#pragma once
#include "Lexer/lexer.hpp"
#include <vector>
#include <string>

namespace Cryo
{
    /**
     * @brief Represents a generic instantiation that needs to be monomorphized
     * 
     * This struct tracks when a generic type like Array<T> is instantiated with
     * concrete types like Array<int>, so the monomorphization pass can generate
     * specialized versions.
     */
    struct GenericInstantiation
    {
        std::string base_name;                    // e.g., "Array"
        std::vector<std::string> concrete_types;  // e.g., ["int"]
        std::string instantiated_name;            // e.g., "Array<int>"
        SourceLocation location;                  // Where this instantiation was used
        
        GenericInstantiation(const std::string& base, const std::vector<std::string>& types, 
                           const std::string& full_name, SourceLocation loc)
            : base_name(base), concrete_types(types), instantiated_name(full_name), location(loc) {}
    };
}