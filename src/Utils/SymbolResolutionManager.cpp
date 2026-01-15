#include "Utils/SymbolResolutionManager.hpp"
#include "AST/Type.hpp"
#include "AST/SymbolTable.hpp"
#include <sstream>

namespace Cryo::SRM {

    // QualifiedIdentifier Implementation
    
    QualifiedIdentifier::QualifiedIdentifier(const std::vector<std::string>& namespaces, 
                                           const std::string& name, 
                                           Cryo::SymbolKind kind)
        : namespace_parts_(namespaces), symbol_name_(name), symbol_kind_(kind) {
        
        // Validate input
        if (name.empty()) {
            throw std::invalid_argument("Symbol name cannot be empty");
        }
        
        // Validate namespace parts
        for (const auto& ns : namespaces) {
            if (ns.empty()) {
                throw std::invalid_argument("Namespace parts cannot be empty");
            }
        }
    }
    
    QualifiedIdentifier::QualifiedIdentifier(const std::string& name, Cryo::SymbolKind kind)
        : QualifiedIdentifier({}, name, kind) {
    }
    
    std::string QualifiedIdentifier::to_string() const {
        if (!cached_string_.has_value()) {
            cached_string_ = build_string_representation();
        }
        return cached_string_.value();
    }
    
    std::string QualifiedIdentifier::to_debug_string() const {
        if (!cached_debug_string_.has_value()) {
            cached_debug_string_ = build_debug_representation();
        }
        return cached_debug_string_.value();
    }
    
    size_t QualifiedIdentifier::hash() const {
        if (!cached_hash_.has_value()) {
            cached_hash_ = calculate_hash();
        }
        return cached_hash_.value();
    }
    
    bool QualifiedIdentifier::equals(const SymbolIdentifier& other) const {
        const auto* other_qualified = dynamic_cast<const QualifiedIdentifier*>(&other);
        if (!other_qualified) return false;
        
        return namespace_parts_ == other_qualified->namespace_parts_ &&
               symbol_name_ == other_qualified->symbol_name_ &&
               symbol_kind_ == other_qualified->symbol_kind_;
    }
    
    std::string QualifiedIdentifier::get_namespace_path() const {
        if (namespace_parts_.empty()) {
            return "";
        }
        
        std::ostringstream oss;
        for (size_t i = 0; i < namespace_parts_.size(); ++i) {
            if (i > 0) oss << "::";
            oss << namespace_parts_[i];
        }
        return oss.str();
    }
    
    std::unique_ptr<QualifiedIdentifier> QualifiedIdentifier::create_simple(
        const std::string& name, Cryo::SymbolKind kind) {
        return std::make_unique<QualifiedIdentifier>(name, kind);
    }
    
    std::unique_ptr<QualifiedIdentifier> QualifiedIdentifier::create_qualified(
        const std::vector<std::string>& namespaces, 
        const std::string& name, 
        Cryo::SymbolKind kind) {
        return std::make_unique<QualifiedIdentifier>(namespaces, name, kind);
    }
    
    void QualifiedIdentifier::invalidate_cache() const {
        cached_string_.reset();
        cached_debug_string_.reset();
        cached_hash_.reset();
    }
    
    std::string QualifiedIdentifier::build_string_representation() const {
        std::ostringstream oss;
        
        if (!namespace_parts_.empty()) {
            for (size_t i = 0; i < namespace_parts_.size(); ++i) {
                if (i > 0) oss << "::";
                oss << namespace_parts_[i];
            }
            oss << "::";
        }
        
        oss << symbol_name_;
        return oss.str();
    }
    
    std::string QualifiedIdentifier::build_debug_representation() const {
        std::ostringstream oss;
        oss << "QualifiedIdentifier{";
        oss << "name=" << symbol_name_;
        oss << ", namespace_path=" << get_namespace_path();
        oss << ", kind=" << static_cast<int>(symbol_kind_);
        oss << "}";
        return oss.str();
    }
    
    size_t QualifiedIdentifier::calculate_hash() const {
        size_t h1 = std::hash<std::string>{}(symbol_name_);
        size_t h2 = std::hash<int>{}(static_cast<int>(symbol_kind_));
        
        for (const auto& ns : namespace_parts_) {
            h1 ^= std::hash<std::string>{}(ns) + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        }
        
        return h1 ^ (h2 << 1);
    }

    // ================================================================================
    // FunctionIdentifier Implementation
    // ================================================================================

    FunctionIdentifier::FunctionIdentifier(const std::vector<std::string>& namespaces,
                                         const std::string& name,
                                         const std::vector<Cryo::Type*>& params,
                                         Cryo::Type* return_type,
                                         FunctionType func_type,
                                         bool is_variadic)
        : QualifiedIdentifier(namespaces, name, Cryo::SymbolKind::Function),
          parameter_types_(params),
          return_type_(return_type),
          function_type_(func_type),
          is_variadic_(is_variadic) {}

    FunctionIdentifier::FunctionIdentifier(const std::string& name,
                                         const std::vector<Cryo::Type*>& params,
                                         Cryo::Type* return_type,
                                         FunctionType func_type,
                                         bool is_variadic)
        : QualifiedIdentifier(name, Cryo::SymbolKind::Function),
          parameter_types_(params),
          return_type_(return_type),
          function_type_(func_type),
          is_variadic_(is_variadic) {}

    std::string FunctionIdentifier::to_string() const {
        std::string base = QualifiedIdentifier::to_string();
        base += "(";
        for (size_t i = 0; i < parameter_types_.size(); ++i) {
            if (i > 0) base += ", ";
            base += parameter_types_[i] ? parameter_types_[i]->display_name() : "unknown";
        }
        base += ")";
        return base;
    }

    std::string FunctionIdentifier::to_debug_string() const {
        return "FunctionIdentifier{" + to_string() + "}";
    }

    std::string FunctionIdentifier::to_overload_key() const {
        std::string key = get_simple_name() + "(";
        for (size_t i = 0; i < parameter_types_.size(); ++i) {
            if (i > 0) key += ",";
            key += parameter_types_[i] ? parameter_types_[i]->display_name() : "unknown";
        }
        key += ")";
        return key;
    }

    std::string FunctionIdentifier::to_constructor_name() const {
        return get_simple_name(); // Constructor name is same as type name
    }

    std::unique_ptr<FunctionIdentifier> FunctionIdentifier::create_constructor(
        const std::vector<std::string>& namespaces,
        const std::string& type_name,
        const std::vector<Cryo::Type*>& param_types) {
        
        return std::make_unique<FunctionIdentifier>(
            namespaces, type_name, param_types, nullptr, FunctionType::Constructor, false);
    }

    size_t FunctionIdentifier::hash() const {
        size_t h1 = QualifiedIdentifier::hash();
        size_t h2 = std::hash<int>{}(static_cast<int>(function_type_));
        
        for (const auto* param : parameter_types_) {
            if (param) {
                h1 ^= std::hash<std::string>{}(param->display_name()) + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
            }
        }
        
        return h1 ^ (h2 << 1);
    }

    bool FunctionIdentifier::equals(const SymbolIdentifier& other) const {
        if (get_identifier_type() != other.get_identifier_type()) {
            return false;
        }
        
        const auto* other_func = static_cast<const FunctionIdentifier*>(&other);
        return QualifiedIdentifier::equals(other) && 
               function_type_ == other_func->function_type_ &&
               parameter_types_.size() == other_func->parameter_types_.size();
    }

    // ================================================================================
    // TypeIdentifier Implementation  
    // ================================================================================

    TypeIdentifier::TypeIdentifier(const std::vector<std::string>& namespaces,
                                 const std::string& name,
                                 Cryo::TypeKind kind,
                                 const std::vector<Cryo::Type*>& template_params)
        : QualifiedIdentifier(namespaces, name, Cryo::SymbolKind::Type),
          template_parameters_(template_params),
          type_kind_(kind),
          is_template_specialization_(!template_params.empty()) {}

    TypeIdentifier::TypeIdentifier(const std::string& name,
                                 Cryo::TypeKind kind,
                                 const std::vector<Cryo::Type*>& template_params)
        : QualifiedIdentifier(name, Cryo::SymbolKind::Type),
          template_parameters_(template_params),
          type_kind_(kind),
          is_template_specialization_(!template_params.empty()) {}

    std::string TypeIdentifier::to_string() const {
        std::string base = QualifiedIdentifier::to_string();
        if (!template_parameters_.empty()) {
            base += "<";
            for (size_t i = 0; i < template_parameters_.size(); ++i) {
                if (i > 0) base += ", ";
                base += template_parameters_[i] ? template_parameters_[i]->display_name() : "unknown";
            }
            base += ">";
        }
        return base;
    }

    std::string TypeIdentifier::to_debug_string() const {
        return "TypeIdentifier{" + to_string() + "}";
    }

    std::string TypeIdentifier::to_template_name() const {
        return to_string(); // Template name includes template parameters
    }

    size_t TypeIdentifier::hash() const {
        size_t h1 = QualifiedIdentifier::hash();
        size_t h2 = std::hash<int>{}(static_cast<int>(type_kind_));
        
        for (const auto* param : template_parameters_) {
            if (param) {
                h1 ^= std::hash<std::string>{}(param->display_name()) + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
            }
        }
        
        return h1 ^ (h2 << 1);
    }

    bool TypeIdentifier::equals(const SymbolIdentifier& other) const {
        if (get_identifier_type() != other.get_identifier_type()) {
            return false;
        }
        
        const auto* other_type = static_cast<const TypeIdentifier*>(&other);
        return QualifiedIdentifier::equals(other) && 
               type_kind_ == other_type->type_kind_ &&
               template_parameters_.size() == other_type->template_parameters_.size();
    }

    // SymbolResolutionContext Implementation
    
    SymbolResolutionContext::SymbolResolutionContext(Cryo::TypeContext* type_context)
        : type_context_(type_context),
          enable_implicit_std_import_(true),
          enable_namespace_fallback_(true) {
        
        // Add standard search paths and imports only if we have std import enabled
        if (enable_implicit_std_import_) {
            add_imported_namespace("std");
        }
    }
    
    void SymbolResolutionContext::push_namespace(const std::string& namespace_name) {
        validate_namespace_name(namespace_name);
        namespace_stack_.push_back(namespace_name);
        clear_namespace_cache();
    }
    
    void SymbolResolutionContext::pop_namespace() {
        if (!namespace_stack_.empty()) {
            namespace_stack_.pop_back();
            clear_namespace_cache();
        }
    }
    
    std::string SymbolResolutionContext::get_current_namespace_path() const {
        if (namespace_stack_.empty()) {
            return "";
        }
        
        std::ostringstream oss;
        for (size_t i = 0; i < namespace_stack_.size(); ++i) {
            if (i > 0) oss << "::";
            oss << namespace_stack_[i];
        }
        return oss.str();
    }
    
    bool SymbolResolutionContext::is_in_namespace(const std::string& namespace_name) const {
        return std::find(namespace_stack_.begin(), namespace_stack_.end(), namespace_name) != namespace_stack_.end();
    }
    
    void SymbolResolutionContext::register_namespace_alias(const std::string& alias, const std::string& full_namespace) {
        namespace_aliases_[alias] = full_namespace;
        clear_namespace_cache();
    }
    
    void SymbolResolutionContext::remove_namespace_alias(const std::string& alias) {
        namespace_aliases_.erase(alias);
        clear_namespace_cache();
    }
    
    std::string SymbolResolutionContext::resolve_namespace_alias(const std::string& alias) const {
        // First check explicit aliases
        auto it = namespace_aliases_.find(alias);
        if (it != namespace_aliases_.end()) {
            return it->second;
        }
        
        // Check if this is an imported namespace that should be treated as an alias
        // For imports like "import Types from <net/types>", "Types" should resolve to "std::net::Types"
        for (const auto& imported_ns : imported_namespaces_) {
            // Extract the last part of the imported namespace
            // Use rfind to find the actual "::" substring, not find_last_of which finds any ':' character
            size_t last_colon = imported_ns.rfind("::");
            std::string ns_suffix;
            if (last_colon != std::string::npos && last_colon + 2 <= imported_ns.length()) {
                ns_suffix = imported_ns.substr(last_colon + 2);
            } else {
                ns_suffix = imported_ns;
            }
            
            if (ns_suffix == alias) {
                return imported_ns;
            }
        }
        
        // No resolution found
        return alias;
    }
    
    bool SymbolResolutionContext::has_namespace_alias(const std::string& alias) const {
        return namespace_aliases_.find(alias) != namespace_aliases_.end();
    }
    
    void SymbolResolutionContext::add_imported_namespace(const std::string& namespace_name) {
        imported_namespaces_.insert(namespace_name);
        clear_namespace_cache();
    }
    
    void SymbolResolutionContext::remove_imported_namespace(const std::string& namespace_name) {
        imported_namespaces_.erase(namespace_name);
        clear_namespace_cache();
    }
    
    bool SymbolResolutionContext::is_namespace_imported(const std::string& namespace_name) const {
        return imported_namespaces_.find(namespace_name) != imported_namespaces_.end();
    }
    
    std::vector<std::unique_ptr<QualifiedIdentifier>> SymbolResolutionContext::generate_lookup_candidates(
        const std::string& symbol_name, Cryo::SymbolKind kind) const {
        
        std::vector<std::unique_ptr<QualifiedIdentifier>> candidates;
        
        // Check if this is a qualified name (contains ::)
        if (symbol_name.find("::") != std::string::npos) {
            auto parsed = Utils::parse_qualified_name(symbol_name);
            std::vector<std::string> ns_parts = parsed.first;
            std::string simple_name = parsed.second;
            
            // Resolve the first namespace part if it's an alias
            if (!ns_parts.empty()) {
                std::string resolved_first = resolve_namespace_alias(ns_parts[0]);
                if (resolved_first != ns_parts[0]) {
                    // Replace the first part with the resolved namespace
                    // Parse the resolved namespace and include ALL parts (not just the "namespace" parts)
                    auto parsed_resolved = Utils::parse_qualified_name(resolved_first);
                    std::vector<std::string> resolved_parts = parsed_resolved.first;
                    // Include the last part from parsing (which is incorrectly treated as "symbol name")
                    if (!parsed_resolved.second.empty()) {
                        resolved_parts.push_back(parsed_resolved.second);
                    }
                    // Append any remaining parts from the original qualified name
                    resolved_parts.insert(resolved_parts.end(), ns_parts.begin() + 1, ns_parts.end());
                    candidates.push_back(std::make_unique<QualifiedIdentifier>(resolved_parts, simple_name, kind));
                }
            }
            
            // Also add the original qualified name as-is
            candidates.push_back(std::make_unique<QualifiedIdentifier>(ns_parts, simple_name, kind));
        } else {
            // Add current namespace path
            candidates.push_back(std::make_unique<QualifiedIdentifier>(namespace_stack_, symbol_name, kind));
            
            // Add imported namespaces
            for (const auto& imported_ns : imported_namespaces_) {
                // Parse the imported namespace and use ALL parts as namespace components
                // For example: "std::IO" should become ["std", "IO"] as namespace parts
                // so that "println" becomes "std::IO::println"
                auto parsed = Utils::parse_qualified_name(imported_ns);
                std::vector<std::string> ns_parts = parsed.first;
                // Include the last part (which parse_qualified_name returns as "symbol name")
                // as part of the namespace path
                if (!parsed.second.empty()) {
                    ns_parts.push_back(parsed.second);
                }
                candidates.push_back(std::make_unique<QualifiedIdentifier>(ns_parts, symbol_name, kind));
            }
        }
        
        // Add parent namespace scopes
        for (size_t i = namespace_stack_.size(); i > 0; --i) {
            std::vector<std::string> parent_ns(namespace_stack_.begin(), namespace_stack_.begin() + i - 1);
            candidates.push_back(std::make_unique<QualifiedIdentifier>(parent_ns, symbol_name, kind));
        }
        
        // Add unqualified name
        candidates.push_back(std::make_unique<QualifiedIdentifier>(symbol_name, kind));
        
        return candidates;
    }
    
    void SymbolResolutionContext::clear_namespace_cache() const {
        resolved_namespace_cache_.clear();
        alias_resolution_cache_.clear();
    }
    
    void SymbolResolutionContext::clear_all_caches() const {
        clear_namespace_cache();
    }
    
    void SymbolResolutionContext::validate_namespace_name(const std::string& namespace_name) const {
        if (namespace_name.empty()) {
            throw std::invalid_argument("Namespace name cannot be empty");
        }
        
        if (!Utils::is_valid_identifier(namespace_name)) {
            throw std::invalid_argument("Invalid namespace name: " + namespace_name);
        }
    }

    std::unique_ptr<TypeIdentifier> SymbolResolutionContext::create_type_identifier(
        const std::string& type_name,
        Cryo::TypeKind kind,
        const std::vector<Cryo::Type*>& template_params) const {

        return std::make_unique<TypeIdentifier>(
            namespace_stack_, type_name, kind, template_params);
    }

    std::unique_ptr<QualifiedIdentifier> SymbolResolutionContext::create_qualified_identifier(
        const std::string& name, Cryo::SymbolKind kind) const {

        return std::make_unique<QualifiedIdentifier>(
            namespace_stack_, name, kind);
    }

    std::unique_ptr<FunctionIdentifier> SymbolResolutionContext::create_function_identifier(
        const std::string& name,
        const std::vector<Cryo::Type*>& params,
        Cryo::Type* return_type,
        FunctionIdentifier::FunctionType func_type) const {

        return std::make_unique<FunctionIdentifier>(
            namespace_stack_, name, params, return_type, func_type);
    }

    void SymbolResolutionContext::dump_context(std::ostream& os) const {
        os << "=== Symbol Resolution Context Dump ===" << std::endl;
        os << "Current Namespace Stack: ";
        if (namespace_stack_.empty()) {
            os << "(global scope)";
        } else {
            for (size_t i = 0; i < namespace_stack_.size(); ++i) {
                if (i > 0) os << "::";
                os << namespace_stack_[i];
            }
        }
        os << std::endl;
        
        os << "Namespace Aliases: ";
        if (namespace_aliases_.empty()) {
            os << "(none)";
        } else {
            for (const auto& [alias, full_ns] : namespace_aliases_) {
                os << alias << " -> " << full_ns << "; ";
            }
        }
        os << std::endl;
        
        os << "Imported Namespaces: ";
        if (imported_namespaces_.empty()) {
            os << "(none)";
        } else {
            for (const auto& ns : imported_namespaces_) {
                os << ns << "; ";
            }
        }
        os << std::endl;
    }

    // SymbolResolutionManager Implementation
    
    SymbolResolutionManager::SymbolResolutionManager(SymbolResolutionContext* context)
        : context_(context), name_generation_attempts_(0), cache_hits_(0), cache_misses_(0),
          namespace_separator_("::"), template_open_("<"), template_close_(">"), llvm_prefix_("cryo_") {
        
        if (!context_) {
            throw std::invalid_argument("SymbolResolutionContext cannot be null");
        }
    }
    
    std::string SymbolResolutionManager::generate_canonical_name(const SymbolIdentifier& identifier) const {
        ++name_generation_attempts_;
        
        // Check cache first
        size_t identifier_hash = calculate_identifier_hash(identifier);
        auto cache_it = canonical_name_cache_.find(identifier_hash);
        if (cache_it != canonical_name_cache_.end()) {
            ++cache_hits_;
            return cache_it->second;
        }
        
        ++cache_misses_;
        
        // Generate canonical name based on identifier type
        std::string canonical_name = build_qualified_name_impl(identifier);
        
        // Cache and return
        canonical_name_cache_[identifier_hash] = canonical_name;
        return canonical_name;
    }
    
    std::string SymbolResolutionManager::generate_llvm_name(const SymbolIdentifier& identifier) const {
        // Check cache first
        size_t identifier_hash = calculate_identifier_hash(identifier);
        auto cache_it = llvm_name_cache_.find(identifier_hash);
        if (cache_it != llvm_name_cache_.end()) {
            return cache_it->second;
        }
        
        // Generate LLVM-compatible name
        std::string canonical = generate_canonical_name(identifier);
        std::string llvm_name = llvm_prefix_ + escape_name_for_llvm(canonical);
        
        // Cache and return
        llvm_name_cache_[identifier_hash] = llvm_name;
        return llvm_name;
    }
    
    std::string SymbolResolutionManager::generate_overload_key(const FunctionIdentifier& identifier) const {
        // Generate unique overload key based on function signature
        std::string base_name = generate_canonical_name(identifier);
        
        // Add parameter types to create unique signature
        std::ostringstream oss;
        oss << base_name << "(";
        
        const auto& params = identifier.get_parameter_types();
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0) oss << ",";
            oss << (params[i] ? params[i]->display_name() : "unknown");
        }
        oss << ")";
        
        // Add return type if available
        if (identifier.get_return_type()) {
            oss << "->" << identifier.get_return_type()->display_name();
        }
        
        return oss.str();
    }
    
    std::string SymbolResolutionManager::generate_template_name(const TypeIdentifier& identifier) const {
        return identifier.to_template_name();
    }
    
    std::string SymbolResolutionManager::generate_constructor_name(
        const TypeIdentifier& type_id,
        const std::vector<Cryo::Type*>& param_types) const {
        
        // Build the constructor name with full namespace qualification
        std::string type_name = type_id.to_string();
        
        // Constructor name is the same as the type name
        std::string constructor_name = type_name + "(";
        
        for (size_t i = 0; i < param_types.size(); ++i) {
            if (i > 0) constructor_name += ",";
            constructor_name += param_types[i] ? param_types[i]->display_name() : "unknown";
        }
        constructor_name += ")";
        
        return constructor_name;
    }
    
    std::string SymbolResolutionManager::generate_default_constructor_name(const TypeIdentifier& type_id) const {
        return generate_constructor_name(type_id, {});
    }
    
    std::vector<std::string> SymbolResolutionManager::generate_lookup_candidates(
        const std::string& base_name, Cryo::SymbolKind kind) const {
        
        auto candidates_ptr = context_->generate_lookup_candidates(base_name, kind);
        std::vector<std::string> candidates;
        
        for (const auto& candidate_ptr : candidates_ptr) {
            if (candidate_ptr) {
                candidates.push_back(candidate_ptr->to_string());
            }
        }
        
        return candidates;
    }
    
    std::vector<std::string> SymbolResolutionManager::generate_function_lookup_candidates(
        const std::string& base_name,
        const std::vector<Cryo::Type*>& param_types) const {
        
        std::vector<std::string> candidates;
        
        // Add base name without parameters
        candidates.push_back(base_name);
        
        // Add name with parameter types for overload resolution
        std::string overload_key = base_name + "(";
        for (size_t i = 0; i < param_types.size(); ++i) {
            if (i > 0) overload_key += ",";
            overload_key += param_types[i] ? param_types[i]->display_name() : "unknown";
        }
        overload_key += ")";
        candidates.push_back(overload_key);
        
        // Add namespace-qualified variants if we're in a namespace context
        auto namespace_variants = generate_all_namespace_variants(base_name);
        candidates.insert(candidates.end(), namespace_variants.begin(), namespace_variants.end());
        
        return candidates;
    }
    
    std::string SymbolResolutionManager::generate_llvm_function_name(const FunctionIdentifier& identifier) const {
        std::string canonical = generate_canonical_name(identifier);
        return llvm_prefix_ + "func_" + escape_name_for_llvm(canonical);
    }
    
    std::string SymbolResolutionManager::generate_llvm_type_name(const TypeIdentifier& identifier) const {
        std::string canonical = generate_canonical_name(identifier);
        return llvm_prefix_ + "type_" + escape_name_for_llvm(canonical);
    }
    
    std::string SymbolResolutionManager::generate_llvm_struct_name(const TypeIdentifier& identifier) const {
        std::string canonical = generate_canonical_name(identifier);
        return "struct." + llvm_prefix_ + escape_name_for_llvm(canonical);
    }
    
    std::string SymbolResolutionManager::generate_llvm_global_name(const std::string& name) const {
        return llvm_prefix_ + "global_" + escape_name_for_llvm(name);
    }
    
    // Configuration methods
    void SymbolResolutionManager::set_namespace_separator(const std::string& separator) {
        namespace_separator_ = separator;
    }
    
    void SymbolResolutionManager::set_template_parameter_format(const std::string& open, const std::string& close) {
        template_open_ = open;
        template_close_ = close;
    }
    
    void SymbolResolutionManager::set_llvm_prefix(const std::string& prefix) {
        llvm_prefix_ = prefix;
    }
    
    void SymbolResolutionManager::clear_name_cache() const {
        canonical_name_cache_.clear();
        llvm_name_cache_.clear();
        name_generation_cache_.clear();
        lookup_candidates_cache_.clear();
    }
    
    void SymbolResolutionManager::clear_all_caches() const {
        clear_name_cache();
    }
    
    SymbolResolutionManager::Statistics SymbolResolutionManager::get_statistics() const {
        Statistics stats;
        stats.total_name_generations = name_generation_attempts_;
        stats.cache_hits = cache_hits_;
        stats.cache_misses = cache_misses_;
        
        if (stats.total_name_generations > 0) {
            stats.cache_hit_ratio = static_cast<double>(stats.cache_hits) / stats.total_name_generations;
        } else {
            stats.cache_hit_ratio = 0.0;
        }
        
        return stats;
    }
    
    void SymbolResolutionManager::reset_statistics() const {
        name_generation_attempts_ = 0;
        cache_hits_ = 0;
        cache_misses_ = 0;
    }
    
    std::string SymbolResolutionManager::get_performance_report() const {
        auto stats = get_statistics();
        std::ostringstream oss;
        oss << "  Total Name Generations: " << stats.total_name_generations << "\n";
        oss << "  Cache Hits: " << stats.cache_hits << "\n";
        oss << "  Cache Misses: " << stats.cache_misses << "\n";
        oss << "  Name Cache Size: " << canonical_name_cache_.size() << "\n";
        oss << "  LLVM Name Cache Size: " << llvm_name_cache_.size() << "\n";
        return oss.str();
    }
    
    std::string SymbolResolutionManager::to_debug_string() const {
        std::ostringstream oss;
        oss << "SymbolResolutionManager{\n";
        oss << "  canonical_name_cache: " << canonical_name_cache_.size() << " entries\n";
        oss << "  llvm_name_cache: " << llvm_name_cache_.size() << " entries\n";
        oss << "  lookup_candidates_cache: " << lookup_candidates_cache_.size() << " entries\n";
        oss << get_performance_report();
        oss << "}";
        return oss.str();
    }
    
    size_t SymbolResolutionManager::calculate_identifier_hash(const SymbolIdentifier& identifier) const {
        return identifier.hash();
    }
    
    // Helper methods for name generation
    std::string SymbolResolutionManager::build_qualified_name_impl(const SymbolIdentifier& identifier) const {
        return identifier.to_string();
    }
    
    std::string SymbolResolutionManager::build_namespace_qualified_name(
        const std::vector<std::string>& namespaces, const std::string& name) const {
        
        if (namespaces.empty()) {
            return name;
        }
        
        std::ostringstream oss;
        for (size_t i = 0; i < namespaces.size(); ++i) {
            if (i > 0) oss << namespace_separator_;
            oss << namespaces[i];
        }
        oss << namespace_separator_ << name;
        return oss.str();
    }
    
    std::string SymbolResolutionManager::escape_name_for_llvm(const std::string& name) const {
        return Utils::escape_for_llvm(name);
    }
    
    std::string SymbolResolutionManager::normalize_identifier_name(const std::string& name) const {
        return Utils::normalize_type_name(name);
    }
    
    std::vector<std::string> SymbolResolutionManager::generate_parent_namespace_variants(const std::string& base_name) const {
        std::vector<std::string> variants;
        
        const auto& namespace_stack = context_->get_namespace_stack();
        
        // Generate variants for each parent namespace level
        for (size_t i = namespace_stack.size(); i > 0; --i) {
            std::vector<std::string> parent_ns(namespace_stack.begin(), namespace_stack.begin() + i - 1);
            std::string qualified_name = build_namespace_qualified_name(parent_ns, base_name);
            variants.push_back(qualified_name);
        }
        
        // Add unqualified name
        variants.push_back(base_name);
        
        return variants;
    }

    std::vector<std::string> SymbolResolutionManager::generate_all_namespace_variants(const std::string& base_name) const {
        std::vector<std::string> variants;
        
        // Get parent namespace variants
        auto parent_variants = generate_parent_namespace_variants(base_name);
        variants.insert(variants.end(), parent_variants.begin(), parent_variants.end());
        
        // Get imported namespace variants  
        auto imported_variants = generate_imported_namespace_variants(base_name);
        variants.insert(variants.end(), imported_variants.begin(), imported_variants.end());
        
        return variants;
    }

    std::vector<std::string> SymbolResolutionManager::generate_imported_namespace_variants(const std::string& base_name) const {
        std::vector<std::string> variants;
        
        // Generate variants from imported namespaces
        for (const auto& imported_ns : context_->get_imported_namespaces()) {
            std::string qualified_name = imported_ns + "::" + base_name;
            variants.push_back(qualified_name);
        }
        
        return variants;
    }

    // ================================================================================
    // Utils Namespace Implementation
    // ================================================================================

    namespace Utils {
        
        std::string build_qualified_name(
            const std::vector<std::string>& namespace_parts, 
            const std::string& symbol_name) {
            
            if (namespace_parts.empty()) {
                return symbol_name;
            }
            
            std::string result;
            for (size_t i = 0; i < namespace_parts.size(); ++i) {
                if (i > 0) result += "::";
                result += namespace_parts[i];
            }
            result += "::" + symbol_name;
            
            return result;
        }
        
        std::pair<std::vector<std::string>, std::string> parse_qualified_name(
            const std::string& qualified_name) {
            
            std::vector<std::string> parts;
            std::string current_part;
            
            // Simple parsing - split on "::"
            size_t pos = 0;
            while (pos < qualified_name.length()) {
                size_t next_pos = qualified_name.find("::", pos);
                if (next_pos == std::string::npos) {
                    current_part = qualified_name.substr(pos);
                    break;
                } else {
                    std::string part = qualified_name.substr(pos, next_pos - pos);
                    if (!part.empty()) {  // Only add non-empty parts
                        parts.push_back(part);
                    }
                    pos = next_pos + 2;
                }
            }
            
            // Last part is the symbol name - use current_part if not empty, otherwise last part from vector
            std::string symbol_name;
            if (!current_part.empty()) {
                symbol_name = current_part;
            } else if (!parts.empty()) {
                symbol_name = parts.back();
                parts.pop_back();
            } else {
                symbol_name = qualified_name; // Fallback for malformed names
            }
            
            return std::make_pair(parts, symbol_name);
        }
        
        std::string escape_for_llvm(const std::string& name) {
            std::string escaped;
            escaped.reserve(name.length() * 2); // Reserve space for potential escaping
            
            for (char c : name) {
                if (std::isalnum(c) || c == '_') {
                    escaped += c;
                } else {
                    // Escape special characters
                    escaped += '_';
                    escaped += std::to_string(static_cast<unsigned char>(c));
                    escaped += '_';
                }
            }
            
            return escaped;
        }
        
        std::string normalize_type_name(const std::string& type_name) {
            std::string normalized = type_name;
            
            // Remove extra whitespace
            size_t pos = 0;
            while ((pos = normalized.find("  ", pos)) != std::string::npos) {
                normalized.replace(pos, 2, " ");
            }
            
            // Trim leading/trailing whitespace
            normalized.erase(0, normalized.find_first_not_of(" \t\n\r"));
            normalized.erase(normalized.find_last_not_of(" \t\n\r") + 1);
            
            return normalized;
        }
        
        bool is_valid_identifier(const std::string& name) {
            if (name.empty()) return false;
            
            // Must start with letter or underscore
            if (!std::isalpha(name[0]) && name[0] != '_') {
                return false;
            }
            
            // Rest must be alphanumeric or underscore
            for (size_t i = 1; i < name.length(); ++i) {
                if (!std::isalnum(name[i]) && name[i] != '_') {
                    return false;
                }
            }
            
            return true;
        }
        
    } // namespace Utils
    
} // namespace Cryo::SRM