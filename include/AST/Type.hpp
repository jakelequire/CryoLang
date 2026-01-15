#pragma once
// Type system is now in Types2 - this file exists only for include compatibility
#include "Types2/Types2.hpp"
#include "Types2/TypeArena.hpp"

namespace Cryo
{
    // Minimal aliases for code that hasn't been migrated yet
    using BooleanType = BoolType;
    using IntegerType = IntType;

    // TypeContext is now TypeArena
    using TypeContext = TypeArena;
}
