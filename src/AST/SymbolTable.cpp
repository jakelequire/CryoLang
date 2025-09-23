#include "AST/SymbolTable.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>

namespace Cryo
{
    bool SymbolTable::declare_symbol(const std::string &name, SymbolKind kind, SourceLocation loc, Type *data_type, const std::string &scope)
    {
        // Check if symbol already exists in current scope
        if (symbols_.find(name) != symbols_.end())
        {
            return false; // Symbol already declared
        }

        // Add the symbol to current scope
        symbols_[name] = Symbol{name, kind, loc, data_type, scope};
        return true;
    }

    Symbol *SymbolTable::lookup_symbol(const std::string &name) const
    {
        // Check current scope first
        auto it = symbols_.find(name);
        if (it != symbols_.end())
        {
            return const_cast<Symbol *>(&it->second);
        }

        // Check parent scopes
        if (parent_scope_)
        {
            return parent_scope_->lookup_symbol(name);
        }

        return nullptr; // Symbol not found
    }

    std::unique_ptr<SymbolTable> SymbolTable::enter_scope()
    {
        // Create new child scope with this as parent
        return std::make_unique<SymbolTable>(std::move(parent_scope_));
    }

    std::unique_ptr<SymbolTable> SymbolTable::exit_scope()
    {
        // Return parent scope
        return std::move(parent_scope_);
    }

    std::string SymbolTable::get_symbol_kind_string(SymbolKind kind) const
    {
        switch (kind)
        {
        case SymbolKind::Variable:
            return "Variable";
        case SymbolKind::Function:
            return "Function";
        case SymbolKind::Type:
            return "Type";
        default:
            return "Unknown";
        }
    }

    void SymbolTable::dump(std::ostream &os, int indent_level) const
    {
        std::string indent(indent_level * 2, ' ');

        if (symbols_.empty())
        {
            os << indent << "(empty scope)" << std::endl;
        }
        else
        {
            for (const auto &[name, symbol] : symbols_)
            {
                std::string type_str = symbol.data_type ? symbol.data_type->to_string() : "unknown";
                os << indent << "|-- " << symbol.name
                   << " [" << get_symbol_kind_string(symbol.kind) << "]"
                   << " (" << type_str << ")"
                   << " at line " << symbol.declaration_location.line()
                   << ", col " << symbol.declaration_location.column()
                   << std::endl;
            }
        }

        // If there's a parent scope, show it with increased indentation
        if (parent_scope_)
        {
            os << indent << "`-- Parent Scope:" << std::endl;
            parent_scope_->dump(os, indent_level + 1);
        }
    }

    void SymbolTable::print_pretty(std::ostream &os) const
    {
        os << "+=========================================================================================+" << std::endl;
        os << "|                                      Symbol Table                                       |" << std::endl;
        os << "+=========================================================================================+" << std::endl;

        if (symbols_.empty() && !parent_scope_)
        {
            os << "|  No symbols found                                                                       |" << std::endl;
            os << "+=========================================================================================+" << std::endl;
            return;
        }

        // Count total symbols across all scopes
        int total_symbols = 0;
        const SymbolTable *current = this;
        while (current)
        {
            total_symbols += current->symbols_.size();
            current = current->parent_scope_.get();
        }

        os << "|  Total symbols: " << total_symbols;
        // Pad to align right side of box
        for (int i = std::to_string(total_symbols).length(); i < 72; i++)
        {
            os << " ";
        }
        os << "|" << std::endl;
        os << "+-----------------------------------------------------------------------------------------+" << std::endl;

        // Header for symbol details - properly aligned with wider columns (Scope and Location swapped)
        os << "| Type     | Name           | Data Type                      | Scope           | Location |" << std::endl;
        os << "+----------+----------------+--------------------------------+-----------------+----------+" << std::endl;

        // Display symbols from current and parent scopes
        print_symbols_table(os, 0);

        os << "+=========================================================================================+" << std::endl;
    }

    void SymbolTable::print_symbols_table(std::ostream &os, int scope_level) const
    {
        // Print symbols in current scope
        for (const auto &[name, symbol] : symbols_)
        {
            std::string kind_str = get_symbol_kind_string(symbol.kind);
            std::string type_str = symbol.data_type ? symbol.data_type->to_string() : "unknown";
            std::string location = std::to_string(symbol.declaration_location.line()) +
                                   ":" + std::to_string(symbol.declaration_location.column());

            os << "| " << format_field(kind_str, 8)
               << " | " << format_field(symbol.name, 14)
               << " | " << format_field(type_str, 30)
               << " | " << format_field(symbol.scope, 15)
               << " | " << format_field(location, 8)
               << " |" << std::endl;
        }

        // Print symbols from parent scopes
        if (parent_scope_)
        {
            parent_scope_->print_symbols_table(os, scope_level + 1);
        }
    }

    std::string SymbolTable::format_field(const std::string &text, int width) const
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

    bool SymbolTable::declare_builtin_function(const std::string &name, const std::string &signature, TypeContext &type_context)
    {
        return declare_builtin_function(name, signature, type_context, "global");
    }

    bool SymbolTable::declare_builtin_function(const std::string &name, const std::string &signature, TypeContext &type_context, const std::string &namespace_scope)
    {
        // Parse the function signature to create proper Type
        Type *function_type = parse_function_signature(signature, type_context);
        if (!function_type)
        {
            std::cout << "    ERROR: Failed to parse function signature for " << name << std::endl;
            return false;
        }
        // Create a dummy source location for built-ins
        SourceLocation loc(0, 0);

        // Register the built-in function with the specified namespace scope
        bool result = declare_symbol(name, SymbolKind::Function, loc, function_type, namespace_scope);
        if (!result)
        {
            std::cout << "    WARNING: Built-in function already declared: " << name << std::endl;
        }
        return result;
    }

    bool SymbolTable::declare_builtin_type(const std::string &name, const std::string &description)
    {
        // For now, just return true - we can implement full type registration later
        return true;
    }

    Type *SymbolTable::parse_function_signature(const std::string &signature, TypeContext &type_context)
    {
        // Parse signature format: "(param1_type, param2_type, ...) -> return_type"

        // Find the arrow separator
        size_t arrow_pos = signature.find(" -> ");
        if (arrow_pos == std::string::npos)
        {
            return nullptr;
        }

        // Extract parameter list (everything before arrow)
        std::string params_str = signature.substr(0, arrow_pos);
        params_str = trim(params_str);

        // Extract return type (everything after arrow)
        std::string return_type_str = signature.substr(arrow_pos + 4); // +4 for " -> "
        return_type_str = trim(return_type_str);

        // Remove parentheses from parameter list
        if (params_str.front() != '(' || params_str.back() != ')')
        {
            return nullptr;
        }
        params_str = params_str.substr(1, params_str.length() - 2);
        params_str = trim(params_str);

        // Convert return type
        Type *return_type = convert_string_to_type(return_type_str, type_context);
        if (!return_type)
        {
            return nullptr;
        }

        // Parse parameter types
        std::vector<Type *> param_types;
        if (!params_str.empty())
        {
            std::istringstream param_stream(params_str);
            std::string param;

            while (std::getline(param_stream, param, ','))
            {
                param = trim(param);
                if (!param.empty())
                {
                    Type *param_type = convert_string_to_type(param, type_context);
                    if (!param_type)
                    {
                        return nullptr;
                    }
                    param_types.push_back(param_type);
                }
            }
        }

        // Create function type
        return type_context.create_function_type(return_type, param_types);
    }

    Type *SymbolTable::convert_string_to_type(const std::string &type_str, TypeContext &type_context)
    {
        std::string clean_type = trim(type_str);

        if (clean_type == "int" || clean_type == "i32")
        {
            return type_context.get_int_type();
        }
        else if (clean_type == "float" || clean_type == "f32")
        {
            return type_context.get_default_float_type();
        }
        else if (clean_type == "string" || clean_type == "char*")
        {
            return type_context.get_string_type();
        }
        else if (clean_type == "bool" || clean_type == "boolean")
        {
            return type_context.get_boolean_type();
        }
        else if (clean_type == "char")
        {
            return type_context.get_char_type();
        }
        else if (clean_type == "void")
        {
            return type_context.get_void_type();
        }
        else if (clean_type == "size_t")
        {
            // Map size_t to int for now
            return type_context.get_int_type();
        }
        else if (clean_type == "array" || clean_type == "cryo_array")
        {
            // For now, create a generic array type - we might need to enhance this
            return type_context.create_array_type(type_context.get_void_type());
        }
        else if (clean_type == "cryo_result")
        {
            // Map cryo_result to a generic type for now
            return type_context.get_void_type();
        }

        // Handle pointer types
        if (clean_type.back() == '*')
        {
            std::string base_type = clean_type.substr(0, clean_type.length() - 1);
            Type *base = convert_string_to_type(base_type, type_context);
            if (base)
            {
                return type_context.create_pointer_type(base);
            }
        }

        return nullptr;
    }

    std::string SymbolTable::trim(const std::string &str)
    {
        size_t first = str.find_first_not_of(' ');
        if (first == std::string::npos)
            return "";
        size_t last = str.find_last_not_of(' ');
        return str.substr(first, (last - first + 1));
    }

}
