#include "AST/SymbolTable.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include "Utils/Logger.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>

namespace Cryo
{
    // ============================================================================
    // Constructor/Destructor
    // ============================================================================
    
    SymbolTable::SymbolTable(std::unique_ptr<SymbolTable> parent, TypeContext *type_context)
        : parent_scope_(std::move(parent)), type_context_(type_context) {
        // Implementation in .cpp file to ensure SRM types are complete when unique_ptr destructors are instantiated
    }
    
    SymbolTable::~SymbolTable() {
        // Default destructor - unique_ptr will handle cleanup automatically
        // The SRM headers are included in this .cpp file so destructor can access complete types
    }
    
    // ============================================================================
    // Symbol Management
    // ============================================================================
    
    bool SymbolTable::declare_symbol(const std::string &name, SymbolKind kind, SourceLocation loc, Type *data_type, const std::string &scope, const std::string &enhanced_display)
    {
        // Check if symbol already exists in current scope
        if (symbols_.find(name) != symbols_.end())
        {
            return false; // Symbol already declared
        }

        // Add the symbol to current scope
        symbols_[name] = Symbol{name, kind, loc, data_type, scope, enhanced_display};
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

    Symbol *SymbolTable::lookup_namespaced_symbol(const std::string &namespace_name, const std::string &symbol_name) const
    {
        // First check current scope for exact match
        auto namespace_it = namespaces_.find(namespace_name);
        if (namespace_it != namespaces_.end())
        {
            auto symbol_it = namespace_it->second.find(symbol_name);
            if (symbol_it != namespace_it->second.end())
            {
                return const_cast<Symbol *>(&symbol_it->second);
            }
        }

        // Check parent scopes for exact match
        if (parent_scope_)
        {
            Symbol *result = parent_scope_->lookup_namespaced_symbol(namespace_name, symbol_name);
            if (result)
                return result;
        }

        return nullptr; // Symbol not found
    }

    Symbol *SymbolTable::lookup_namespaced_symbol_with_context(const std::string &namespace_name, const std::string &symbol_name, const std::string &current_namespace) const
    {
        // First try exact match
        Symbol *result = lookup_namespaced_symbol(namespace_name, symbol_name);
        if (result)
            return result;

        // If in a namespace, try relative resolution
        if (!current_namespace.empty() && current_namespace != "Global")
        {
            // Extract parent namespace(s)
            // For "std::IO" we want "std", for "std::Syscall::IO" we want "std::Syscall"
            size_t last_scope = current_namespace.find_last_of(':');

            if (last_scope != std::string::npos && last_scope >= 1)
            {
                // Extract parent by removing "::component" from end
                // "std::IO" -> find_last_of(':') = 4, so substr(0, 4-1) = "std"
                std::string parent_namespace = current_namespace.substr(0, last_scope - 1);

                // Try parent::namespace_name (e.g., "std" + "::" + "Syscall" = "std::Syscall")
                std::string qualified_namespace = Cryo::SRM::Utils::build_qualified_name({parent_namespace}, namespace_name);
                result = lookup_namespaced_symbol(qualified_namespace, symbol_name);

                LOG_TRACE(Cryo::LogComponent::AST, "Trying relative resolution: {}::{} (from context {})",
                          qualified_namespace, symbol_name, current_namespace);

                if (result)
                    return result;
            }
        }

        return nullptr; // Symbol not found with context
    }

    Symbol *SymbolTable::lookup_symbol_in_any_namespace(const std::string &symbol_name) const
    {
        // Search through all registered namespaces
        for (const auto &[namespace_name, symbols] : namespaces_)
        {
            auto symbol_it = symbols.find(symbol_name);
            if (symbol_it != symbols.end())
            {
                LOG_TRACE(Cryo::LogComponent::AST, "Found symbol '{}' in namespace '{}'", symbol_name, namespace_name);
                return const_cast<Symbol *>(&symbol_it->second);
            }
        }

        // Check parent scopes
        if (parent_scope_)
        {
            return parent_scope_->lookup_symbol_in_any_namespace(symbol_name);
        }

        return nullptr; // Symbol not found in any namespace
    }

    Symbol *SymbolTable::lookup_symbol_with_import_resolution(const std::string &symbol_name, const std::vector<std::string> &imported_namespaces) const
    {
        // First try local scope
        Symbol *result = lookup_symbol(symbol_name);
        if (result)
            return result;

        // Then try each imported namespace
        for (const std::string &namespace_name : imported_namespaces)
        {
            result = lookup_namespaced_symbol(namespace_name, symbol_name);
            if (result)
            {
                LOG_TRACE(Cryo::LogComponent::AST, "Found symbol '{}' in imported namespace '{}'", symbol_name, namespace_name);
                return result;
            }
        }

        // Finally fall back to any namespace search
        return lookup_symbol_in_any_namespace(symbol_name);
    }

    Symbol *SymbolTable::lookup_qualified_symbol_with_import_shortcuts(const std::string &qualified_name, const std::vector<std::string> &imported_namespaces) const
    {
        // First try the full qualified name as-is
        size_t scope_pos = qualified_name.find("::");
        if (scope_pos != std::string::npos)
        {
            std::string namespace_part = qualified_name.substr(0, scope_pos);
            std::string symbol_part = qualified_name.substr(scope_pos + 2);

            // Try exact lookup first
            Symbol *result = lookup_namespaced_symbol(namespace_part, symbol_part);
            if (result)
                return result;

            // Try expanding short namespace names with imported namespaces
            for (const std::string &imported_namespace : imported_namespaces)
            {
                // Check if this imported namespace ends with the requested namespace part
                // e.g., "Memory" matches "std::Memory"
                if (imported_namespace.length() > namespace_part.length())
                {
                    size_t suffix_pos = imported_namespace.length() - namespace_part.length();
                    if (imported_namespace.substr(suffix_pos) == namespace_part)
                    {
                        // Check that it's preceded by "::" or is at the start
                        if (suffix_pos == 0 || imported_namespace.substr(suffix_pos - 2, 2) == "::")
                        {
                            result = lookup_namespaced_symbol(imported_namespace, symbol_part);
                            if (result)
                            {
                                LOG_TRACE(Cryo::LogComponent::AST, "Expanded '{}' to '{}::{}'", qualified_name, imported_namespace, symbol_part);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        return nullptr;
    }

    void SymbolTable::register_namespace(const std::string &namespace_name, const std::unordered_map<std::string, Symbol> &symbols)
    {
        namespaces_[namespace_name] = symbols;
        LOG_DEBUG(Cryo::LogComponent::AST, "Registered namespace '{}' with {} symbols", namespace_name, symbols.size());
    }

    bool SymbolTable::has_namespace(const std::string &namespace_name) const
    {
        auto it = namespaces_.find(namespace_name);
        if (it != namespaces_.end())
        {
            return true;
        }

        // Check parent scopes
        if (parent_scope_)
        {
            return parent_scope_->has_namespace(namespace_name);
        }

        return false;
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
        case SymbolKind::Intrinsic:
            return "Intrinsic";
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
            // Use enhanced display if available, otherwise use data_type->to_string()
            std::string type_str;
            if (!symbol.enhanced_display.empty())
            {
                type_str = symbol.enhanced_display;
            }
            else
            {
                type_str = symbol.data_type ? symbol.data_type->to_string() : "unknown";
            }

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
            LOG_ERROR(Cryo::LogComponent::AST, "Failed to parse function signature for {}", name);
            return false;
        }
        // Create a dummy source location for built-ins
        SourceLocation loc(0, 0);

        // Register the built-in function with the specified namespace scope
        bool result = declare_symbol(name, SymbolKind::Function, loc, function_type, namespace_scope);
        if (!result)
        {
            LOG_WARN(Cryo::LogComponent::AST, "Built-in function already declared: {}", name);
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
        else if (clean_type == "boolean")
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

    // SRM integration methods implementation
    
    SRM::SymbolResolutionContext* SymbolTable::get_srm_context() const {
        initialize_srm();
        return srm_context_.get();
    }
    
    SRM::SymbolResolutionManager* SymbolTable::get_srm_manager() const {
        initialize_srm();
        return srm_manager_.get();
    }
    
    void SymbolTable::initialize_srm() const {
        if (!srm_context_) {
            // Initialize SRM context with current symbol table
            srm_context_ = std::make_unique<SRM::SymbolResolutionContext>(
                const_cast<SymbolTable*>(this), type_context_);
            
            // Configure default settings
            srm_context_->set_enable_implicit_std_import(true);
            srm_context_->set_enable_namespace_fallback(true);
        }
        
        if (!srm_manager_) {
            // Initialize SRM manager with the context
            srm_manager_ = std::make_unique<SRM::SymbolResolutionManager>(srm_context_.get());
        }
    }
    
    Symbol* SymbolTable::lookup_symbol_srm(const SRM::SymbolIdentifier& identifier) const {
        auto result = get_srm_manager()->resolve_symbol(identifier);
        return result.is_valid() ? result.symbol : nullptr;
    }
    
    std::vector<Symbol*> SymbolTable::find_symbol_overloads(
        const std::string& base_name, const std::vector<Type*>& arg_types) const {
        
        auto results = get_srm_manager()->resolve_function_overloads(base_name, arg_types);
        
        std::vector<Symbol*> symbols;
        for (const auto& result : results) {
            if (result.is_valid()) {
                symbols.push_back(result.symbol);
            }
        }
        
        return symbols;
    }
    
    Symbol* SymbolTable::find_best_function_overload(
        const std::string& base_name, const std::vector<Type*>& arg_types) const {
        
        auto result = get_srm_manager()->resolve_best_function_overload(base_name, arg_types);
        return result.is_valid() ? result.symbol : nullptr;
    }
    
    bool SymbolTable::declare_symbol_srm(
        std::unique_ptr<SRM::SymbolIdentifier> identifier, 
        SymbolKind kind, 
        SourceLocation loc, 
        Type* data_type, 
        const std::string& scope) {
        
        if (!identifier) return false;
        
        // Use the SRM identifier's canonical name for registration
        std::string canonical_name = identifier->to_string();
        
        // Register with traditional symbol table
        bool success = declare_symbol(canonical_name, kind, loc, data_type, scope);
        
        if (success) {
            // Also register with SRM for enhanced resolution
            get_srm_manager()->register_generated_symbol(std::move(identifier), lookup_symbol(canonical_name));
        }
        
        return success;
    }
    
    void SymbolTable::configure_srm(bool enable_implicit_std, bool enable_namespace_fallback) const {
        initialize_srm();
        srm_context_->set_enable_implicit_std_import(enable_implicit_std);
        srm_context_->set_enable_namespace_fallback(enable_namespace_fallback);
    }
    
    void SymbolTable::add_srm_namespace_alias(const std::string& alias, const std::string& full_namespace) const {
        initialize_srm();
        srm_context_->register_namespace_alias(alias, full_namespace);
    }
    
    void SymbolTable::add_srm_imported_namespace(const std::string& namespace_name) const {
        initialize_srm();
        srm_context_->add_imported_namespace(namespace_name);
    }
    
    void SymbolTable::dump_srm_state(std::ostream& os) const {
        if (srm_context_) {
            os << "=== SRM Context State ===" << std::endl;
            srm_context_->dump_context(os);
            os << std::endl;
        }
        
        if (srm_manager_) {
            os << "=== SRM Manager State ===" << std::endl;
            os << srm_manager_->to_debug_string() << std::endl;
            os << srm_manager_->get_performance_report() << std::endl;
        }
    }

}
