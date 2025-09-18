#include "AST/ASTContext.hpp"
#include "AST/ASTNode.hpp"
#include "AST/SymbolTable.hpp"

namespace Cryo
{
    ASTContext::ASTContext()
        : _symbol_table(std::make_unique<SymbolTable>()) {}

    ASTContext::~ASTContext() = default;
}