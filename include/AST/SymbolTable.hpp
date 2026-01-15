#pragma once
// SymbolTable is now in Types2 - this file exists only for include compatibility
#include "Types2/SymbolTable2.hpp"

namespace Cryo
{
    // SymbolTable is now SymbolTable2
    using SymbolTable = SymbolTable2;
    using Symbol = Symbol2;
    using SymbolKind = SymbolKind2;
}
