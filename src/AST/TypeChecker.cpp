#include "AST/TypeChecker.hpp"
#include "AST/SymbolTable.hpp"
#include "Lexer/lexer.hpp"
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
            return &it->second;
        }

        // Search parent scopes
        if (_parent_scope)
        {
            return _parent_scope->lookup_symbol(name);
        }

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
        : _type_context(type_ctx)
    {
        _symbol_table = std::make_unique<TypedSymbolTable>();
        _type_registry = std::make_unique<TypeRegistry>(&type_ctx);

        // Register common generic types
        register_generic_type("Array", {"T"});
        register_generic_type("Option", {"T"});
        register_generic_type("Result", {"T", "E"});
    }

    //===----------------------------------------------------------------------===//
    // Generic Context Management
    //===----------------------------------------------------------------------===//

    void TypeChecker::enter_generic_context(const std::string &type_name,
                                            const std::vector<std::string> &parameters,
                                            SourceLocation location)
    {
        std::cout << "[DEBUG] TypeChecker: Entering generic context for '" << type_name
                  << "' with parameters: ";
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            std::cout << parameters[i];
            if (i < parameters.size() - 1)
                std::cout << ", ";
        }
        std::cout << std::endl;

        _generic_context_stack.emplace_back(type_name, parameters, location);
    }

    void TypeChecker::exit_generic_context()
    {
        if (!_generic_context_stack.empty())
        {
            const auto &context = _generic_context_stack.back();
            std::cout << "[DEBUG] TypeChecker: Exiting generic context for '"
                      << context.type_name << "'" << std::endl;
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
        std::cout << "[DEBUG] TypeChecker::resolve_type_with_generic_context: '" << type_string << "'" << std::endl;

        // First check if this is a generic parameter
        if (is_generic_parameter(type_string))
        {
            std::cout << "[DEBUG] TypeChecker: '" << type_string
                      << "' is a generic parameter, creating generic type" << std::endl;
            return _type_context.get_generic_type(type_string);
        }

        // Check for tuple types first (before checking for generic syntax)
        if (type_string.front() == '(' && type_string.back() == ')')
        {
            std::cout << "[DEBUG] TypeChecker: '" << type_string << "' is a tuple type, deferring to type context" << std::endl;
            // Tuple types should be handled by the main type context, not as generic instantiations
            return _type_context.parse_type_from_string(type_string);
        }

        // Check for reference types (&Type<T>) - strip the & for template lookup
        std::string clean_type_string = type_string;
        bool is_reference = false;
        if (type_string.front() == '&')
        {
            clean_type_string = type_string.substr(1);
            is_reference = true;
            std::cout << "[DEBUG] TypeChecker: Stripped reference prefix, checking template for: '" << clean_type_string << "'" << std::endl;
        }

        // Check for parameterized types like ptr<T>, Option<T>, Iterator<T>
        if (clean_type_string.find('<') != std::string::npos)
        {
            std::cout << "[DEBUG] TypeChecker: Found generic type syntax in '" << clean_type_string << "'" << std::endl;

            // Parse the base type and type arguments
            size_t open_bracket = clean_type_string.find('<');
            size_t close_bracket = clean_type_string.find('>', open_bracket);

            if (close_bracket != std::string::npos)
            {
                std::string base_type = clean_type_string.substr(0, open_bracket);
                std::string type_args_str = clean_type_string.substr(open_bracket + 1,
                                                                     close_bracket - open_bracket - 1);

                std::cout << "[DEBUG] TypeChecker: base_type='" << base_type << "', type_args='" << type_args_str << "'" << std::endl;

                // Check if base_type is a type alias first
                TypedSymbol *alias_symbol = _symbol_table->lookup_symbol(base_type);
                if (alias_symbol && alias_symbol->type && alias_symbol->type->kind() == TypeKind::TypeAlias)
                {
                    std::cout << "[DEBUG] TypeChecker: Found type alias '" << base_type << "', expanding..." << std::endl;
                    // For type aliases like AllocResult<T> = Result<T*, AllocError>
                    // We need to expand AllocResult<void> to Result<void*, AllocError>

                    // Get the target type string from the alias
                    if (auto alias_type = dynamic_cast<TypeAlias *>(alias_symbol->type))
                    {
                        std::string target_type_str = alias_type->target_type()->to_string();
                        std::cout << "[DEBUG] TypeChecker: Alias target type: '" << target_type_str << "'" << std::endl;

                        // For now, expand manually for AllocResult
                        if (base_type == "AllocResult" && type_args_str == "void")
                        {
                            std::string expanded_type = "Result<void*, AllocError>";
                            std::cout << "[DEBUG] TypeChecker: Expanding " << clean_type_string << " to " << expanded_type << std::endl;
                            return resolve_type_with_generic_context(expanded_type);
                        }
                    }
                }

                // Check if any type arguments are generic parameters
                bool has_generic_args = is_generic_parameter(type_args_str);

                std::cout << "[DEBUG] TypeChecker: has_generic_args=" << has_generic_args
                          << ", is_in_generic_context=" << is_in_generic_context() << std::endl;

                if (has_generic_args)
                {
                    std::cout << "[DEBUG] TypeChecker: '" << type_string
                              << "' contains generic parameters, deferring resolution" << std::endl;
                    // For template definitions with generic parameters, we don't create concrete types yet
                    return _type_context.get_generic_type(type_string);
                }
                else if (!is_in_generic_context())
                {
                    // Only track concrete instantiations when we're NOT defining a generic type
                    // This prevents tracking during template definition parsing
                    std::vector<std::string> concrete_types = parse_template_arguments(type_args_str);

                    std::cout << "[DEBUG] TypeChecker: Tracking concrete instantiation: " << clean_type_string
                              << " (base: " << base_type << ", args: " << type_args_str << ")" << std::endl;
                    std::cout << "[DEBUG] TypeChecker: Parsed " << concrete_types.size() << " concrete types:";
                    for (const auto &type : concrete_types)
                    {
                        std::cout << " '" << type << "'";
                    }
                    std::cout << std::endl;

                    // Track the base template (without &), but record the clean type name for instantiation
                    track_instantiation(base_type, concrete_types, clean_type_string, SourceLocation{});
                }
            }
        }

        // Fall back to normal type parsing
        return _type_context.parse_type_from_string(type_string);
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

        std::cout << "[DEBUG] TypeChecker: Tracking instantiation '" << instantiated_name
                  << "' (base: " << base_name << ")" << std::endl;

        _required_instantiations.emplace_back(base_name, concrete_types, instantiated_name, location);
    }

    const std::vector<GenericInstantiation> &TypeChecker::get_required_instantiations() const
    {
        return _required_instantiations;
    }

    void TypeChecker::load_builtin_symbols(const SymbolTable &main_symbol_table)
    {
        // Store reference to main symbol table for scope lookups
        _main_symbol_table = &main_symbol_table;

        std::cout << "Loading builtin symbols into TypeChecker..." << std::endl;
        std::cout << "Main symbol table has " << main_symbol_table.get_symbols().size() << " symbols" << std::endl;

        // Copy all built-in function symbols from main symbol table
        int copied_count = 0;
        for (const auto &[name, symbol] : main_symbol_table.get_symbols())
        {
            if ((symbol.kind == SymbolKind::Function || symbol.kind == SymbolKind::Intrinsic) && symbol.data_type != nullptr)
            {
                _symbol_table->declare_symbol(name, symbol.data_type, symbol.declaration_location);
                copied_count++;
            }
        }
        std::cout << "Copied " << copied_count << " builtin functions to TypeChecker symbol table" << std::endl;
    }

    void TypeChecker::load_intrinsic_symbols(const SymbolTable &main_symbol_table)
    {
        std::cout << "Loading intrinsic symbols into TypeChecker..." << std::endl;

        // Copy only intrinsic symbols from main symbol table
        int copied_count = 0;
        for (const auto &[name, symbol] : main_symbol_table.get_symbols())
        {
            if (symbol.kind == SymbolKind::Intrinsic && symbol.data_type != nullptr)
            {
                _symbol_table->declare_symbol(name, symbol.data_type, symbol.declaration_location);
                copied_count++;
                std::cout << "Loaded intrinsic: " << name << std::endl;
            }
        }
        std::cout << "Copied " << copied_count << " intrinsic symbols to TypeChecker symbol table" << std::endl;
    }

    void TypeChecker::load_user_symbols(const SymbolTable &main_symbol_table)
    {
        std::cout << "Loading user-defined symbols into TypeChecker..." << std::endl;

        // Copy user-defined function symbols from main symbol table
        int copied_count = 0;
        for (const auto &[name, symbol] : main_symbol_table.get_symbols())
        {
            if (symbol.kind == SymbolKind::Function && symbol.data_type != nullptr)
            {
                _symbol_table->declare_symbol(name, symbol.data_type, symbol.declaration_location);
                copied_count++;
                std::cout << "Loaded user function: " << name << std::endl;
            }
        }
        std::cout << "Copied " << copied_count << " user symbols to TypeChecker symbol table" << std::endl;
    }

    void TypeChecker::load_runtime_symbols(const SymbolTable &main_symbol_table)
    {
        std::cout << "Loading runtime symbols into TypeChecker..." << std::endl;

        // Debug: Show all symbols in the main symbol table
        std::cout << "DEBUG: Main symbol table contains " << main_symbol_table.get_symbols().size() << " symbols:" << std::endl;
        for (const auto &[name, symbol] : main_symbol_table.get_symbols())
        {
            std::cout << "  - " << name << " (kind: " << static_cast<int>(symbol.kind) << ")" << std::endl;
        }

        // Access runtime symbols from the std::Runtime namespace
        int copied_count = 0;
        const auto &namespaces = main_symbol_table.get_namespaces();
        auto runtime_ns_it = namespaces.find("std::Runtime");

        if (runtime_ns_it != namespaces.end())
        {
            const auto &runtime_symbols = runtime_ns_it->second;
            std::cout << "DEBUG: Found std::Runtime namespace with " << runtime_symbols.size() << " symbols" << std::endl;

            // Only load symbols that have valid type information to avoid crashes
            for (const auto &[name, symbol] : runtime_symbols)
            {
                if (symbol.kind == SymbolKind::Function && symbol.data_type != nullptr)
                {
                    // Register the function globally without namespace qualification
                    _symbol_table->declare_symbol(name, symbol.data_type, symbol.declaration_location);
                    copied_count++;
                    std::cout << "Loaded runtime function globally: " << name << std::endl;
                }
                else if (symbol.kind == SymbolKind::Function)
                {
                    std::cout << "DEBUG: Skipping runtime function '" << name << "' - no type information (will be resolved via namespace during function calls)" << std::endl;
                }
            }
        }
        else
        {
            std::cout << "DEBUG: std::Runtime namespace not found" << std::endl;
            std::cout << "Available namespaces:" << std::endl;
            for (const auto &[ns_name, symbols] : namespaces)
            {
                std::cout << "  - " << ns_name << " (" << symbols.size() << " symbols)" << std::endl;
            }
        }

        std::cout << "Copied " << copied_count << " runtime symbols to TypeChecker symbol table" << std::endl;

        // Note: Runtime functions without type information will be resolved via namespace lookup
        // during function call analysis. This is actually the preferred approach since it allows
        // the type system to resolve the correct function signature at call sites.
        std::cout << "NOTE: Runtime functions will be resolved via std::Runtime namespace during function calls" << std::endl;
    }

    void TypeChecker::register_generic_type(const std::string &base_name, const std::vector<std::string> &param_names)
    {
        _type_registry->register_template(base_name, param_names);
    }

    ParameterizedType *TypeChecker::resolve_generic_type(const std::string &type_string)
    {
        return _type_registry->parse_and_instantiate(type_string);
    }

    void TypeChecker::check_program(ProgramNode &program)
    {
        // Clear any previous state
        _errors.clear();
        _warnings.clear();
        _current_function_return_type = nullptr;
        _in_function = false;
        _in_loop = false;

        // Visit all top-level declarations and statements
        for (const auto &stmt : program.statements())
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
        }
    }

    void TypeChecker::check_imported_modules(const std::unordered_map<std::string, std::unique_ptr<ProgramNode>>& imported_asts)
    {
        std::cout << "[DEBUG] TypeChecker: Processing " << imported_asts.size() << " imported modules for AST node updates" << std::endl;
        
        // Open debug file for detailed logging
        std::ofstream debug_file("logs/debug_imported_modules.txt", std::ios::app);
        
        // Debug: Let's see what symbols are actually in the TypedSymbolTable
        debug_file << "=== Current symbols in TypedSymbolTable ===" << std::endl;
        auto all_symbols = _symbol_table->get_symbols();
        for (const auto& [name, symbol] : all_symbols) {
            debug_file << "Symbol: '" << name << "' type: " << (symbol.type ? symbol.type->to_string() : "null") << std::endl;
        }
        debug_file << "=== End of symbol dump ===" << std::endl;
        
        // Debug: List all imported modules
        for (const auto& [module_name, ast] : imported_asts)
        {
            std::cout << "[DEBUG] TypeChecker: Available imported module: '" << module_name << "'" << std::endl;
            debug_file << "Available imported module: '" << module_name << "'" << std::endl;
        }
        
        for (const auto& [module_name, ast] : imported_asts)
        {
            if (!ast)
            {
                std::cout << "[DEBUG] TypeChecker: Skipping null AST for module: " << module_name << std::endl;
                continue;
            }

            std::cout << "[DEBUG] TypeChecker: Processing imported module: " << module_name << std::endl;
            std::cout << "[DEBUG] TypeChecker: Module has " << ast->statements().size() << " statements" << std::endl;
            
            // Visit all top-level declarations in the imported module
            // This will update existing symbols with their AST node references
            int stmt_count = 0;
            for (const auto &stmt : ast->statements())
            {
                if (stmt)
                {
                    std::cout << "[DEBUG] TypeChecker: Processing statement " << (++stmt_count) << " in module " << module_name << std::endl;
                    debug_file << "Processing statement " << stmt_count << " in module " << module_name << std::endl;
                    
                    // Debug: What kind of statement is this?
                    const char* node_type = "unknown";
                    switch(stmt->kind()) {
                        case NodeKind::FunctionDeclaration: node_type = "FunctionDeclaration"; break;
                        case NodeKind::ImportDeclaration: node_type = "ImportDeclaration"; break;
                        case NodeKind::VariableDeclaration: node_type = "VariableDeclaration"; break;
                        case NodeKind::StructDeclaration: node_type = "StructDeclaration"; break;
                        case NodeKind::EnumDeclaration: node_type = "EnumDeclaration"; break;
                        case NodeKind::IntrinsicDeclaration: node_type = "IntrinsicDeclaration"; break;
                        case NodeKind::ExternBlock: node_type = "ExternBlock"; break;
                        default: node_type = "other"; break;
                    }
                    std::cout << "[DEBUG] TypeChecker: Statement type: " << node_type << std::endl;
                    debug_file << "Statement type: " << node_type << std::endl;
                    
                    // For function declarations, we need to check with qualified names
                    if (stmt->kind() == NodeKind::FunctionDeclaration) {
                        if (auto func_decl = dynamic_cast<FunctionDeclarationNode*>(stmt.get())) {
                            std::string func_name = func_decl->name();
                            std::string qualified_name = module_name + "::" + func_name;
                            
                            debug_file << "Function '" << func_name << "' qualified as '" << qualified_name << "'" << std::endl;
                            
                            // Try multiple lookup strategies for imported module functions
                            TypedSymbol *existing_symbol = nullptr;
                            
                            // Strategy 1: Look up unqualified name
                            existing_symbol = _symbol_table->lookup_symbol(func_name);
                            if (existing_symbol) {
                                debug_file << "Found '" << func_name << "' via unqualified lookup" << std::endl;
                            } else {
                                // Strategy 2: Look up fully qualified name
                                existing_symbol = _symbol_table->lookup_symbol(qualified_name);
                                if (existing_symbol) {
                                    debug_file << "Found '" << func_name << "' via qualified lookup as '" << qualified_name << "'" << std::endl;
                                } else {
                                    // Strategy 3: Check namespaces
                                    existing_symbol = _symbol_table->lookup_symbol_in_any_namespace(func_name);
                                    if (existing_symbol) {
                                        debug_file << "Found '" << func_name << "' via namespace lookup" << std::endl;
                                    } else {
                                        debug_file << "Function '" << func_name << "' not found in symbol table" << std::endl;
                                    }
                                }
                            }
                            
                            if (existing_symbol) {
                                debug_file << "Updating function '" << func_name << "' with AST node reference" << std::endl;
                                existing_symbol->function_node = func_decl;
                            }
                        }
                    } else {
                        // For non-function statements, use normal visitor
                        stmt->accept(*this);
                    }
                }
            }
        }
        
        std::cout << "[DEBUG] TypeChecker: Finished processing imported modules" << std::endl;
        debug_file << "=== Finished processing imported modules ===" << std::endl;
        debug_file.close();
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

        // Parse type annotation from node
        if (!node.type_annotation().empty() && node.type_annotation() != "auto")
        {
            declared_type = resolve_type_with_generic_context(node.type_annotation());
        }

        if (node.initializer())
        {
            // Visit the initializer to determine its type
            node.initializer()->accept(*this);
            inferred_type = node.initializer()->type().has_value()
                                ? _type_context.parse_type_from_string(node.initializer()->type().value())
                                : nullptr;
        }

        // Determine final type
        Type *final_type = nullptr;
        if (declared_type && inferred_type)
        {
            // Both declared and inferred - check compatibility
            if (!check_assignment_compatibility(declared_type, inferred_type, node.location()))
            {
                report_type_mismatch(node.location(), declared_type, inferred_type,
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
            report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                         "Cannot infer type for variable '" + var_name + "'");
            final_type = _type_context.get_unknown_type();
        }

        // Declare the variable in symbol table
        if (!declare_variable(var_name, final_type, node.location(), node.is_mutable()))
        {
            report_redefined_symbol(node.location(), var_name);
        }
    }

    void TypeChecker::visit(FunctionDeclarationNode &node)
    {
        const std::string &func_name = node.name();
        
        // Open debug file for detailed logging
        std::ofstream debug_file("logs/debug_function_visitor.txt", std::ios::app);

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
            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                         "Unable to parse return type: " + return_type_str);
            return_type = _type_context.get_unknown_type();
        }

        // Collect parameter types
        std::vector<Type *> param_types;
        for (const auto &param : node.parameters())
        {
            if (param)
            {
                const std::string &param_type_str = param->type_annotation();
                Type *param_type = resolve_type_with_generic_context(param_type_str);
                if (!param_type)
                {
                    report_error(TypeError::ErrorKind::InvalidOperation, param->location(),
                                 "Unable to parse parameter type: " + param_type_str);
                    param_type = _type_context.get_unknown_type();
                }
                param_types.push_back(param_type);
            }
        }

        // Create function type
        FunctionType *func_type = static_cast<FunctionType *>(
            _type_context.create_function_type(return_type, param_types));

        // Declare function in current scope if not already declared
        std::cout << "[DEBUG] Looking up function symbol: '" << func_name << "'" << std::endl;
        debug_file << "Looking up function symbol: '" << func_name << "'" << std::endl;
        TypedSymbol *existing_symbol = _symbol_table->lookup_symbol(func_name);
        if (!existing_symbol) {
            std::cout << "[DEBUG] Not found in main symbols, checking namespaces..." << std::endl;
            debug_file << "Not found in main symbols, checking namespaces..." << std::endl;
            // Also check if it might be in a namespace (IO functions)
            TypedSymbol *ns_symbol = _symbol_table->lookup_symbol_in_any_namespace(func_name);
            if (ns_symbol) {
                std::cout << "[DEBUG] Found '" << func_name << "' in namespaces!" << std::endl;
                debug_file << "Found '" << func_name << "' in namespaces!" << std::endl;
                existing_symbol = ns_symbol; // Use the namespaced symbol
            } else {
                std::cout << "[DEBUG] '" << func_name << "' not found in any namespace either" << std::endl;
                debug_file << "'" << func_name << "' not found in any namespace either" << std::endl;
            }
        }
        if (existing_symbol)
        {
            // Function already exists (likely from load_user_symbols), verify type compatibility
            std::cout << "[DEBUG] Function '" << func_name << "' already exists" << std::endl;
            std::cout << "[DEBUG] Existing type: " << (existing_symbol->type ? existing_symbol->type->to_string() : "null") << std::endl;
            std::cout << "[DEBUG] New type: " << (func_type ? func_type->to_string() : "null") << std::endl;
            if (existing_symbol->type != func_type)
            {
                std::cout << "[DEBUG] Types don't match - checking type equality more deeply" << std::endl;
                // Types might be functionally equivalent but different objects
                // For now, allow redefinition if it's the same function
                std::cout << "[DEBUG] Allowing redefinition for now" << std::endl;
            }
            // Function exists with compatible type - update with AST node reference for LSP
            std::cout << "[DEBUG] Updating existing function '" << func_name << "' with AST node reference" << std::endl;
            debug_file << "Updating existing function '" << func_name << "' with AST node reference" << std::endl;
            existing_symbol->function_node = &node;
            debug_file.close();
        }
        else
        {
            // Function doesn't exist yet, declare it
            if (!declare_function(func_name, func_type, node.location(), &node))
            {
                report_redefined_symbol(node.location(), func_name);
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
            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                         "Return statement outside of function");
            return;
        }

        if (node.expression())
        {
            // Check return expression type
            node.expression()->accept(*this);

            if (node.expression()->type().has_value())
            {
                Type *expr_type = _type_context.parse_type_from_string(
                    node.expression()->type().value());

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
                report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                             "Function must return a value of type '" +
                                 _current_function_return_type->to_string() + "'");
            }
        }
    }

    void TypeChecker::visit(IfStatementNode &node)
    {
        // Check condition type
        if (node.condition())
        {
            node.condition()->accept(*this);

            if (node.condition()->type().has_value())
            {
                Type *cond_type = _type_context.parse_type_from_string(
                    node.condition()->type().value());

                if (!is_boolean_context_valid(cond_type))
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

            if (node.condition()->type().has_value())
            {
                Type *cond_type = _type_context.parse_type_from_string(
                    node.condition()->type().value());

                if (!is_boolean_context_valid(cond_type))
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

            if (node.condition()->type().has_value())
            {
                Type *cond_type = _type_context.parse_type_from_string(
                    node.condition()->type().value());

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
            node.set_type(literal_type->to_string());
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
                node.set_type(_current_struct_type->to_string());
            }
            else
            {
                report_undefined_symbol(node.location(), name);
                node.set_type(_type_context.get_unknown_type()->to_string());
            }
            return;
        }

        // Special handling for primitive type constructors
        if (is_primitive_integer_type(name))
        {
            // Primitive type constructors are functions that take one argument and return the target type
            // e.g., i64 is a function (T) -> i64 where T is any integer type
            std::string function_type = "(any) -> " + name;
            node.set_type(function_type);
            return;
        }

        TypedSymbol *symbol = _symbol_table->lookup_symbol(name);
        if (!symbol)
        {
            // Try to find the symbol in the std::Runtime namespace as a fallback
            // This allows runtime functions like cryo_alloc to be used without qualification
            if (_main_symbol_table)
            {
                Symbol *runtime_symbol = _main_symbol_table->lookup_namespaced_symbol_with_context("std::Runtime", name, _current_namespace);
                if (runtime_symbol)
                {
                    // Found runtime function - create a basic function type signature
                    if (runtime_symbol->kind == SymbolKind::Function)
                    {
                        // For runtime functions without type info, provide a more reasonable signature
                        // Most runtime functions like cryo_alloc return pointers
                        std::string function_type;
                        if (name == "cryo_alloc")
                        {
                            function_type = "(u64) -> void*";
                        }
                        else if (name == "cryo_free")
                        {
                            function_type = "(void*) -> void";
                        }
                        else
                        {
                            // Generic fallback for other runtime functions
                            function_type = "(...) -> void";
                        }
                        node.set_type(function_type);
                        return;
                    }
                }
            }

            report_undefined_symbol(node.location(), name);
            node.set_type(_type_context.get_unknown_type()->to_string());
        }
        else
        {
            node.set_type(symbol->type->to_string());
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

            if (node.left()->type().has_value())
            {
                left_type = _type_context.parse_type_from_string(node.left()->type().value());
            }

            if (node.right()->type().has_value())
            {
                right_type = _type_context.parse_type_from_string(node.right()->type().value());
            }

            if (left_type && right_type)
            {
                TokenKind op = node.operator_token().kind();

                // Check for assignment operations
                if (op == TokenKind::TK_EQUAL || op == TokenKind::TK_PLUSEQUAL || op == TokenKind::TK_MINUSEQUAL ||
                    op == TokenKind::TK_STAREQUAL || op == TokenKind::TK_SLASHEQUAL)
                {
                    // For assignment, check if right is assignable to left
                    if (!left_type->is_assignable_from(*right_type))
                    {
                        report_error(TypeError::ErrorKind::InvalidAssignment, node.location(),
                                     "Type mismatch in assignment: cannot assign " +
                                         right_type->name() + " to " + left_type->name());
                    }
                    // Assignment result has the type of the left operand
                    node.set_type(left_type->name());
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
                                report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                             "Type mismatch in arithmetic operation: " +
                                                 left_type->name() + " and " + right_type->name());
                            }
                        }
                        else
                        {
                            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                         "Type mismatch in arithmetic operation: " +
                                             left_type->name() + " and " + right_type->name());
                        }
                    }
                    // Comparison operations
                    else if (op == TokenKind::TK_EQUALEQUAL || op == TokenKind::TK_EXCLAIMEQUAL ||
                             op == TokenKind::TK_L_ANGLE || op == TokenKind::TK_R_ANGLE ||
                             op == TokenKind::TK_LESSEQUAL || op == TokenKind::TK_GREATEREQUAL)
                    {
                        if (left_type->is_assignable_from(*right_type) || right_type->is_assignable_from(*left_type))
                        {
                            result_type = _type_context.get_boolean_type();
                        }
                        else
                        {
                            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                         "Cannot compare incompatible types: " +
                                             left_type->name() + " and " + right_type->name());
                        }
                    }

                    if (result_type)
                    {
                        node.set_type(result_type->name());
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

            Type *operand_type = node.operand()->type().has_value()
                                     ? _type_context.parse_type_from_string(node.operand()->type().value())
                                     : nullptr;

            if (operand_type)
            {
                TokenKind op = node.operator_token().kind();

                if (op == TokenKind::TK_AMP) // Address-of operator (&)
                {
                    // Create a pointer type to the operand type
                    Type *pointer_type = _type_context.create_pointer_type(operand_type);
                    node.set_type(pointer_type->to_string());
                }
                else if (op == TokenKind::TK_STAR) // Dereference operator (*)
                {
                    // Operand should be a pointer or reference type
                    if (operand_type->kind() == TypeKind::Pointer)
                    {
                        // Get the pointee type
                        auto *ptr_type = static_cast<PointerType *>(operand_type);
                        node.set_type(ptr_type->pointee_type()->to_string());
                    }
                    else if (operand_type->kind() == TypeKind::Reference)
                    {
                        // Get the referent type
                        auto *ref_type = static_cast<ReferenceType *>(operand_type);
                        node.set_type(ref_type->referent_type()->to_string());
                    }
                    else
                    {
                        report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                     "Cannot dereference non-pointer/reference type: " + operand_type->to_string());
                    }
                }
                else if (op == TokenKind::TK_MINUS) // Unary minus
                {
                    if (is_numeric_type(operand_type))
                    {
                        node.set_type(operand_type->to_string());
                    }
                    else
                    {
                        report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                     "Unary minus cannot be applied to non-numeric type: " + operand_type->to_string());
                    }
                }
                else if (op == TokenKind::TK_EXCLAIM) // Logical NOT
                {
                    if (is_boolean_context_valid(operand_type))
                    {
                        node.set_type(_type_context.get_boolean_type()->to_string());
                    }
                    else
                    {
                        report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                     "Logical NOT cannot be applied to non-boolean type: " + operand_type->to_string());
                    }
                }
                else if (op == TokenKind::TK_PLUSPLUS) // Increment operator (++ prefix or postfix)
                {
                    if (is_numeric_type(operand_type))
                    {
                        // For both prefix and postfix increment, the result type is the same as operand type
                        node.set_type(operand_type->to_string());
                    }
                    else
                    {
                        report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                     "Increment operator cannot be applied to non-numeric type: " + operand_type->to_string());
                    }
                }
                else if (op == TokenKind::TK_MINUSMINUS) // Decrement operator (-- prefix or postfix)
                {
                    if (is_numeric_type(operand_type))
                    {
                        // For both prefix and postfix decrement, the result type is the same as operand type
                        node.set_type(operand_type->to_string());
                    }
                    else
                    {
                        report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                     "Decrement operator cannot be applied to non-numeric type: " + operand_type->to_string());
                    }
                }
                else
                {
                    report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                 "Unknown unary operator: " + node.operator_token().to_string());
                }
            }
        }
    }

    void TypeChecker::visit(CallExpressionNode &node)
    {
        // Visit callee
        if (node.callee())
        {
            node.callee()->accept(*this);
        }

        // Visit arguments
        std::vector<Type *> arg_types;
        for (const auto &arg : node.arguments())
        {
            if (arg)
            {
                arg->accept(*this);
                if (arg->type().has_value())
                {
                    Type *arg_type = _type_context.parse_type_from_string(arg->type().value());
                    arg_types.push_back(arg_type);
                }
                else
                {
                    arg_types.push_back(_type_context.get_unknown_type());
                }
            }
        }

        // Check if callee is callable
        if (node.callee() && node.callee()->type().has_value())
        {
            // Special handling for ScopeResolution callees
            if (node.callee()->kind() == NodeKind::ScopeResolution)
            {
                std::string callee_type_str = node.callee()->type().value();

                // Check if this is a function signature (contains "->")
                if (callee_type_str.find(" -> ") != std::string::npos)
                {
                    // This is a function call - extract the return type
                    size_t arrow_pos = callee_type_str.find(" -> ");
                    std::string return_type_str = callee_type_str.substr(arrow_pos + 4);
                    node.set_type(return_type_str);
                    return;
                }
                else
                {
                    // This is likely an enum constructor - use the original logic
                    node.set_type(callee_type_str);
                    return;
                }
            }

            // Special handling for method calls (MemberAccess as callee)
            if (node.callee()->kind() == NodeKind::MemberAccess)
            {
                // For method calls, the MemberAccess already contains the return type
                std::string return_type = node.callee()->type().value();
                node.set_type(return_type);
                return;
            }

            std::string callee_type_str = node.callee()->type().value();

            // Skip reparsing for function types - the type was already correctly computed
            // during symbol declaration. Just verify it looks like a function signature.
            if (callee_type_str.find("->") != std::string::npos || callee_type_str.find("(") == 0)
            {
                // This is a function type string - trust it and proceed
                // For now, we'll assume all well-formed function signatures are callable
                // TODO: Implement proper return type extraction from signature

                // Extract return type from function signature like "(int, int) -> int"
                size_t arrow_pos = callee_type_str.find(" -> ");
                if (arrow_pos != std::string::npos)
                {
                    std::string return_type_str = callee_type_str.substr(arrow_pos + 4);
                    node.set_type(return_type_str);
                }
                else
                {
                    // Fallback for malformed signatures
                    node.set_type("unknown");
                }
                return;
            }

            // For non-function types, use the original parsing logic
            Type *callee_type = _type_context.parse_type_from_string(callee_type_str);

            if (callee_type->kind() != TypeKind::Function)
            {
                report_error(TypeError::ErrorKind::NonCallableType, node.location(),
                             "Expression is not callable");
                node.set_type(_type_context.get_unknown_type()->to_string());
                return;
            }

            // Check function call compatibility
            FunctionType *func_type = static_cast<FunctionType *>(callee_type);
            if (check_function_call_compatibility(func_type, arg_types, node.location()))
            {
                node.set_type(func_type->return_type()->to_string());
            }
            else
            {
                node.set_type(_type_context.get_unknown_type()->to_string());
            }
        }
        else
        {
            node.set_type(_type_context.get_unknown_type()->to_string());
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
            report_error(TypeError::ErrorKind::UndefinedVariable, node.location(),
                         "Undefined type '" + node.type_name() + "' in constructor call");
            node.set_type("unknown");
        }

        // TODO: Add constructor argument validation
        // For now, we assume any struct/class can be constructed with any arguments
        // Later we can add proper constructor signature checking
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
        else if (type_name == "bool")
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
            report_error(TypeError::ErrorKind::UndefinedVariable, node.location(),
                         "Undefined type '" + type_name + "' in sizeof expression");
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
        // Visit the object expression first
        if (node.object())
        {
            node.object()->accept(*this);
        }

        // Get the type of the object being accessed
        std::string object_type = node.object() && node.object()->type().has_value()
                                      ? node.object()->type().value()
                                      : "unknown";

        if (object_type == "unknown")
        {
            node.set_type("unknown");
            return;
        }

        // Get the member name
        std::string member_name = node.member();

        // Handle pointer types - automatically dereference for member access
        std::string effective_type = object_type;
        bool is_pointer_access = false;
        if (object_type.back() == '*' && object_type.length() > 1)
        {
            // This is a pointer type, extract the pointee type
            effective_type = object_type.substr(0, object_type.length() - 1);
            is_pointer_access = true;
        }

        // Check if this is an array type (ends with [])
        bool is_array_type = (effective_type.find("[]") != std::string::npos);

        // Handle built-in array properties
        if (is_array_type)
        {
            if (member_name == "length")
            {
                node.set_type("u64");
                return;
            }
            else if (member_name == "size")
            {
                node.set_type("u64");
                return;
            }
            // For now, only support built-in array properties
            report_error(TypeError::ErrorKind::UndefinedVariable, node.location(),
                         "Unknown array property '" + member_name + "' for type '" + effective_type + "'");
            node.set_type("unknown");
            return;
        }

        // For generic types, extract the base type name for field/method lookup
        std::string lookup_type = effective_type;
        size_t generic_pos = effective_type.find('<');
        if (generic_pos != std::string::npos)
        {
            lookup_type = effective_type.substr(0, generic_pos);
        }

        // Look up the type by name to get struct/class information
        Type *struct_type = lookup_variable_type(lookup_type);
        bool is_primitive_type = (lookup_type == "string" || lookup_type == "int" || lookup_type == "i8" ||
                                  lookup_type == "i16" || lookup_type == "i32" || lookup_type == "i64" ||
                                  lookup_type == "uint" || lookup_type == "u8" || lookup_type == "u16" ||
                                  lookup_type == "u32" || lookup_type == "u64" || lookup_type == "float" ||
                                  lookup_type == "f32" || lookup_type == "f64" || lookup_type == "double" ||
                                  lookup_type == "boolean" || lookup_type == "bool" || lookup_type == "char" ||
                                  lookup_type == "void");

        // Check if this is a generic type (contains '<' and '>')
        bool is_generic_type = (effective_type.find('<') != std::string::npos &&
                                effective_type.find('>') != std::string::npos);

        if (!struct_type && !is_primitive_type && !is_generic_type)
        {
            std::string display_type = is_pointer_access ? object_type + " (dereferenced from pointer)" : object_type;
            report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                         "Cannot access member of non-struct/class type: " + display_type);
            node.set_type("unknown");
            return;
        }

        // For non-primitive types, verify it's a struct/class/generic type
        if (struct_type && !is_primitive_type &&
            struct_type->kind() != TypeKind::Struct &&
            struct_type->kind() != TypeKind::Class &&
            struct_type->kind() != TypeKind::Generic)
        {
            std::string display_type = is_pointer_access ? object_type + " (dereferenced from pointer)" : object_type;
            report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                         "Cannot access member of non-struct/class type: " + display_type);
            node.set_type("unknown");
            return;
        }

        // Look up field in struct field map
        auto struct_it = _struct_fields.find(lookup_type);
        if (struct_it != _struct_fields.end())
        {
            auto field_it = struct_it->second.find(member_name);
            if (field_it != struct_it->second.end())
            {
                // Found the field - substitute generic type parameters if needed
                std::string field_type = field_it->second->to_string();
                std::string resolved_type = substitute_generic_type(field_type, effective_type, lookup_type);
                node.set_type(resolved_type);
                return;
            }
        }

        // Look up method in struct method map
        auto method_struct_it = _struct_methods.find(lookup_type);
        if (method_struct_it != _struct_methods.end())
        {
            auto method_it = method_struct_it->second.find(member_name);
            if (method_it != method_struct_it->second.end())
            {
                // Found the method - substitute generic type parameters if needed
                std::string method_return_type = method_it->second->to_string();
                std::string resolved_type = substitute_generic_type(method_return_type, effective_type, lookup_type);
                node.set_type(resolved_type);
                return;
            }
        }

        // Check for private methods within the same class/struct context
        // Look in the private method registry
        std::cout << "[DEBUG] Looking up private method '" << member_name
                  << "' in lookup_type '" << lookup_type << "'" << std::endl;

        // First check if we're in a class context and the lookup_type matches current class
        if (!_current_struct_name.empty() && lookup_type == _current_struct_name)
        {
            auto private_method_struct_it = _private_struct_methods.find(lookup_type);
            if (private_method_struct_it != _private_struct_methods.end())
            {
                auto private_method_it = private_method_struct_it->second.find(member_name);
                if (private_method_it != private_method_struct_it->second.end())
                {
                    std::cout << "[DEBUG] Found private method '" << member_name << "' in current class '" << lookup_type << "'" << std::endl;
                    std::string method_return_type = private_method_it->second->to_string();
                    std::string resolved_type = substitute_generic_type(method_return_type, object_type, lookup_type);
                    node.set_type(resolved_type);
                    return;
                }
            }

            // If not found in registered methods, check if we're currently processing the class definition
            // In this case, allow the private method call to proceed
            std::cout << "[DEBUG] Allowing private method '" << member_name << "' in current class context '" << lookup_type << "'" << std::endl;
            node.set_type("void"); // Default to void for private methods
            return;
        }

        auto private_method_struct_it = _private_struct_methods.find(lookup_type);
        if (private_method_struct_it != _private_struct_methods.end())
        {
            std::cout << "[DEBUG] Found private methods for type '" << lookup_type << "'" << std::endl;
            auto private_method_it = private_method_struct_it->second.find(member_name);
            if (private_method_it != private_method_struct_it->second.end())
            {
                std::cout << "[DEBUG] Found private method '" << member_name << "'" << std::endl;
                // Found the private method - substitute generic type parameters if needed
                std::string method_return_type = private_method_it->second->to_string();
                std::string resolved_type = substitute_generic_type(method_return_type, object_type, lookup_type);
                node.set_type(resolved_type);
                return;
            }
            else
            {
                std::cout << "[DEBUG] Private method '" << member_name << "' not found in type '" << lookup_type << "'" << std::endl;
            }
        }
        else
        {
            std::cout << "[DEBUG] No private methods found for type '" << lookup_type << "'" << std::endl;
        }

        // For primitive types, also check if there are functions available in global scope
        // that might be primitive method implementations from imported modules
        if (is_primitive_type)
        {
            // First, try to find if we have the primitive type methods imported from stdlib
            // Check if we have core::Types imported (which would contain primitive implementations)
            if (_main_symbol_table)
            {
                // Look through all registered namespaces for potential primitive method implementations
                std::string primitive_method_name = lookup_type + "::" + member_name;

                // Also try checking if the method exists as a global function (for compatibility)
                auto symbol = _main_symbol_table->lookup_symbol(member_name);
                if (symbol && symbol->kind == SymbolKind::Function)
                {
                    // Found a function with the right name - assume it's the primitive method
                    // For primitive methods, we'll set appropriate return types
                    if (member_name == "length")
                    {
                        node.set_type("u64");
                        return;
                    }
                    else if (member_name == "char_at")
                    {
                        node.set_type("char");
                        return;
                    }
                    else
                    {
                        node.set_type("unknown"); // Fallback for other methods
                        return;
                    }
                }

                // If we have imported core/types (which contains primitive implementations),
                // assume primitive methods are available
                if (_main_symbol_table->has_namespace("std::core::Types") && lookup_type == "string")
                {
                    // We have core/types imported and this is a string method call
                    // Assume common primitive methods are available
                    if (member_name == "length")
                    {
                        node.set_type("u64");
                        return;
                    }
                    else if (member_name == "char_at")
                    {
                        node.set_type("char");
                        return;
                    }
                }
            }
        }

        // Check for methods in registered class templates
        // For now, hardcode known template methods - TODO: make this dynamic
        if (lookup_type == "Array" && is_generic_type)
        {
            if (member_name == "size")
            {
                std::cout << "[DEBUG] Found template method 'size' in Array<T>" << std::endl;
                node.set_type("u64");
                return;
            }
            else if (member_name == "push")
            {
                std::cout << "[DEBUG] Found template method 'push' in Array<T>" << std::endl;
                node.set_type("void");
                return;
            }
            else if (member_name == "get")
            {
                std::cout << "[DEBUG] Found template method 'get' in Array<T>" << std::endl;
                // Extract the element type from Array<T>
                size_t start = object_type.find('<');
                size_t end = object_type.find('>');
                if (start != std::string::npos && end != std::string::npos && end > start)
                {
                    std::string element_type = object_type.substr(start + 1, end - start - 1);
                    std::string option_type = "Option<" + element_type + ">";
                    node.set_type(option_type);
                    return;
                }
                node.set_type("Option<unknown>");
                return;
            }
        }

        // Field or method not found in struct
        report_error(TypeError::ErrorKind::UndefinedVariable, node.location(),
                     "Unknown member '" + member_name + "' in type '" + object_type + "'");
        node.set_type("unknown");
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
                    // std::cout << "[DEBUG] Resolved namespace symbol: " << scope_name << "::" << member_name
                    // << " with generic type: " << type_name
                    // << " (context: " << _current_namespace << ")" << std::endl;
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
                        // std::cout << "[DEBUG] Resolved static method: " << scope_name << "::" << member_name
                        // << " -> i64" << std::endl;
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
                        // std::cout << "[DEBUG] Resolved static method: " << scope_name << "::" << member_name
                        // << " -> int" << std::endl;
                        return;
                    }
                    else
                    {
                        // Generic static method - assume it exists
                        node.set_type("unknown");
                        // std::cout << "[DEBUG] Resolved generic static method: " << scope_name << "::" << member_name
                        // << " -> unknown" << std::endl;
                        return;
                    }
                }
                else
                {
                    // std::cout << "[DEBUG] Class " << class_name << " not found in namespace " << namespace_part << std::endl;
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
                // std::cout << "[DEBUG] Resolved generic static method: " << scope_name << "::" << member_name
                // << " -> " << scope_name << std::endl;
                return;
            }

            // For other static methods, use unknown type for now
            node.set_type("unknown");
            // std::cout << "[DEBUG] Resolved generic static method: " << scope_name << "::" << member_name
            // << " -> unknown (trait bounds: ";
            for (size_t i = 0; i < trait_names.size(); ++i)
            {
                if (i > 0)
                    std::cout << ", ";
                std::cout << trait_names[i];
            }
            std::cout << ")" << std::endl;
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
        std::cout << "[DEBUG] Looking up scope symbol '" << base_scope_name << "'" << std::endl;

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
                    std::cout << "[DEBUG] Found generic type '" << base_scope_name << "' in main symbol table" << std::endl;
                    // For enum types, allow the scope resolution to proceed
                    return; // Found the type, proceed with resolution
                }

                // Also try main symbol table namespaces
                main_symbol = _main_symbol_table->lookup_symbol_in_any_namespace(base_scope_name);
                if (main_symbol && main_symbol->data_type)
                {
                    std::cout << "[DEBUG] Found generic type '" << base_scope_name << "' in main symbol table namespace" << std::endl;
                    return; // Found the type, proceed with resolution
                }
            }

            // If not found yet, check if it's a registered generic type in our type registry
            if (!scope_symbol)
            {
                ParameterizedType *template_type = _type_registry->get_template(base_scope_name);
                if (template_type)
                {
                    std::cout << "[DEBUG] Found generic type '" << base_scope_name << "' in type registry" << std::endl;
                    return; // Found the type, proceed with resolution
                }
            }

            // If still not found, this might be a forward reference within the same module
            // For common types like Option, Result etc, allow them to be resolved
            if (!scope_symbol && (base_scope_name == "Option" || base_scope_name == "Result" || base_scope_name == "Array"))
            {
                std::cout << "[DEBUG] Allowing forward reference for common generic type '" << base_scope_name << "'" << std::endl;
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
                    std::cout << "[DEBUG] Found generic type '" << base_scope_name << "' in type registry (non-generic case)" << std::endl;
                    return; // Found the type, proceed with resolution
                }
            }
        }

        if (!scope_symbol)
        {
            std::cout << "[DEBUG] Scope symbol '" << base_scope_name << "' not found" << std::endl;
            std::string qualified_name = scope_name + "::" + member_name;
            report_undefined_symbol(node.location(), qualified_name);
            node.set_type(_type_context.get_unknown_type()->to_string());
            return;
        }

        Type *scope_type = scope_symbol->type;
        if (!scope_type)
        {
            report_undefined_symbol(node.location(), base_scope_name);
            node.set_type(_type_context.get_unknown_type()->to_string());
            return;
        }

        // Verify it's an enum type
        if (scope_type->kind() != TypeKind::Enum)
        {
            report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                         "Scope resolution '::' can only be used with enum types or namespace functions, got: " + base_scope_name);
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
            report_redefined_symbol(node.location(), struct_name);
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

        // Process methods
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
            report_redefined_symbol(node.location(), class_name);
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
                report_undefined_symbol(node.location(), base_class_name);
            }
            else
            {
                Type *base_class_type = base_symbol->type;
                if (!base_class_type || base_class_type->kind() != TypeKind::Class)
                {
                    report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                                 "Base class must be a valid class type");
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

        // Process methods
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
            report_redefined_symbol(node.location(), trait_name);
            return;
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
                report_error(TypeError::ErrorKind::UndefinedVariable, node.location(), "Undefined parent trait '" + base_trait.name + "'");
            }
            else if (parent_symbol->type->kind() != TypeKind::Trait)
            {
                report_error(TypeError::ErrorKind::IncompatibleTypes, node.location(), "'" + base_trait.name + "' is not a trait");
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

        // TraitDeclarationNode doesn't have set_type method, so we don't call it
    }

    void TypeChecker::visit(TypeAliasDeclarationNode &node)
    {
        std::string alias_name = node.alias_name();

        // Check for redefinition
        if (_symbol_table->lookup_symbol(alias_name))
        {
            report_redefined_symbol(node.location(), alias_name);
            return;
        }

        // Handle generic type aliases differently
        if (node.is_generic())
        {
            // For now, create a type alias that represents the generic template
            // In a full implementation, we'd need proper template system with type parameter substitution
            // std::cout << "[DEBUG] Generic type alias: " << alias_name << "<";
            const auto &params = node.generic_params();
            for (size_t i = 0; i < params.size(); ++i)
            {
                if (i > 0)
                    std::cout << ", ";
                std::cout << params[i];
            }
            std::cout << "> = " << node.target_type() << std::endl;

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
            Type *target_type = _type_context.parse_type_from_string(node.target_type());
            if (!target_type)
            {
                target_type = _type_context.get_unknown_type();
            }

            Type *generic_alias = _type_context.create_type_alias(full_signature, target_type);
            _symbol_table->declare_symbol(alias_name, generic_alias, node.location(), false);
            return;
        }

        std::string target_type_str = node.target_type();

        // Handle forward declarations (empty target type)
        if (target_type_str.empty())
        {
            // Forward declaration - create TypeAlias with unknown target for now
            Type *forward_alias = _type_context.create_type_alias(alias_name, _type_context.get_unknown_type());
            _symbol_table->declare_symbol(alias_name, forward_alias, node.location(), false);
            return;
        }

        // Visit target type to ensure it's valid
        Type *target_type = _type_context.parse_type_from_string(target_type_str);

        if (target_type && target_type->kind() != TypeKind::Unknown)
        {
            // Create TypeAlias wrapping the target type
            Type *alias_type = _type_context.create_type_alias(alias_name, target_type);
            _symbol_table->declare_symbol(alias_name, alias_type, node.location(), false);
        }
        else
        {
            report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                         "Invalid target type for type alias: " + target_type_str);
        }
    }

    void TypeChecker::visit(EnumDeclarationNode &node)
    {
        std::string enum_name = node.name();

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
                        _symbol_table->declare_symbol(variant_name, int_type, variant->location(), false);
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
            report_redefined_symbol(node.location(), enum_name);
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
                            Type *param_type = _type_context.parse_type_from_string(type_str);
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
                Type *type = _type_context.parse_type_from_string(type_str);
                if (!type || type->kind() == TypeKind::Unknown)
                {
                    report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                                 "Invalid type in enum variant: " + type_str);
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
                base_type_name == "boolean" || base_type_name == "bool" || base_type_name == "char" ||
                base_type_name == "void")
            {
                is_primitive_type = true;
                // Create a primitive type using type context
                target_type = _type_context.parse_type_from_string(base_type_name);
            }
            else
            {
                // Look up the base type in symbol table
                target_type = lookup_variable_type(base_type_name);
                if (!target_type)
                {
                    report_undefined_symbol(node.location(), base_type_name);
                    return;
                }

                // Verify it's a struct, class, enum, or trait type
                if (target_type->kind() != TypeKind::Struct &&
                    target_type->kind() != TypeKind::Class &&
                    target_type->kind() != TypeKind::Enum &&
                    target_type->kind() != TypeKind::Trait)
                {
                    report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                                 "Implementation block can only be applied to struct, class, enum, or trait types");
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
            report_redefined_symbol(node.location(), param_name);
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
            report_redefined_symbol(node.location(), field_name);
            return;
        }

        // Get field type from type annotation
        std::string field_type_str = node.field_type();
        Type *field_type = _type_context.parse_type_from_string(field_type_str);

        if (field_type && field_type->kind() != TypeKind::Unknown)
        {
            // Register field in current scope (struct/class scope)
            _symbol_table->declare_symbol(field_name, field_type, node.location(), node.is_mutable());

            // Store field information for later member access resolution
            if (!_current_struct_name.empty())
            {
                _struct_fields[_current_struct_name][field_name] = field_type;
            }

            node.set_type(field_type_str);
        }
        else
        {
            report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                         "Invalid field type: " + field_type_str);
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
            report_redefined_symbol(node.location(), method_name);
            return;
        }

        // Special handling for constructors to avoid name conflicts with struct/class type
        if (is_constructor)
        {
            // Process constructor parameters and body manually to avoid registering
            // the constructor function with the same name as the struct/class type

            const std::string &func_name = node.name();

            // Parse return type from node annotation (constructors have void return typically)
            const std::string &return_type_str = node.return_type_annotation().empty()
                                                     ? "void"
                                                     : node.return_type_annotation();
            Type *return_type = _type_context.parse_type_from_string(return_type_str);

            if (!return_type)
            {
                report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                             "Unable to parse return type: " + return_type_str);
                return_type = _type_context.get_unknown_type();
            }

            // Collect parameter types
            std::vector<Type *> param_types;
            for (const auto &param : node.parameters())
            {
                if (param)
                {
                    const std::string &param_type_str = param->type_annotation();
                    Type *param_type = _type_context.parse_type_from_string(param_type_str);
                    if (!param_type)
                    {
                        report_error(TypeError::ErrorKind::InvalidOperation, param->location(),
                                     "Unable to parse parameter type: " + param_type_str);
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

        // Store method information for member access resolution
        if (!_current_struct_name.empty())
        {
            // Get the return type from the node
            std::string return_type_str = node.return_type_annotation();
            if (return_type_str.empty() && is_constructor)
            {
                return_type_str = "void"; // Constructors typically return void
            }
            Type *return_type = _type_context.parse_type_from_string(return_type_str);

            if (return_type)
            {
                // Register in appropriate registry based on visibility
                if (node.visibility() == Visibility::Private)
                {
                    std::cout << "[DEBUG] Registering private method '" << method_name
                              << "' for struct '" << _current_struct_name << "'" << std::endl;
                    _private_struct_methods[_current_struct_name][method_name] = return_type;
                }
                else
                {
                    _struct_methods[_current_struct_name][method_name] = return_type;
                }
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

        if (node.left() && node.left()->type().has_value())
        {
            lhs_type = _type_context.parse_type_from_string(node.left()->type().value());
        }

        if (node.right() && node.right()->type().has_value())
        {
            rhs_type = _type_context.parse_type_from_string(node.right()->type().value());
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
             (lhs_type->kind() == TypeKind::Float && rhs_type->is_integral())))
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
        if (!func_type)
            return false;

        const auto &param_types = func_type->parameter_types();

        // Check argument count
        if (arg_types.size() < param_types.size())
        {
            report_error(TypeError::ErrorKind::TooFewArguments, loc,
                         "Too few arguments in function call");
            return false;
        }

        if (arg_types.size() > param_types.size() && !func_type->is_variadic())
        {
            report_error(TypeError::ErrorKind::TooManyArguments, loc,
                         "Too many arguments in function call");
            return false;
        }

        // Check argument types
        for (size_t i = 0; i < param_types.size(); ++i)
        {
            if (!check_assignment_compatibility(param_types[i].get(), arg_types[i], loc))
            {
                report_type_mismatch(loc, param_types[i].get(), arg_types[i],
                                     "function argument " + std::to_string(i + 1));
                return false;
            }
        }

        return true;
    }

    //===----------------------------------------------------------------------===//
    // Error Reporting
    //===----------------------------------------------------------------------===//

    void TypeChecker::report_error(TypeError::ErrorKind kind, SourceLocation loc, const std::string &message)
    {
        _errors.emplace_back(kind, loc, message);
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
        std::string message = "Type mismatch in " + context;
        _errors.emplace_back(TypeError::ErrorKind::TypeMismatch, loc, message, expected, actual);
    }

    void TypeChecker::report_undefined_symbol(SourceLocation loc, const std::string &symbol_name)
    {
        std::string message = "Undefined symbol '" + symbol_name + "'";
        _errors.emplace_back(TypeError::ErrorKind::UndefinedVariable, loc, message);
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
            report_undefined_symbol(node.location(), enum_name);
            return;
        }

        Type *enum_type = enum_symbol->type;
        if (!enum_type || enum_type->kind() != TypeKind::Enum)
        {
            report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                         "Expected enum type, got: " + enum_name);
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
