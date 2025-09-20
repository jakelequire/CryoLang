#include "AST/ASTContext.hpp"
#include "AST/ASTNode.hpp"
#include "AST/SymbolTable.hpp"
#include "AST/Type.hpp"

namespace Cryo
{
    ASTContext::ASTContext()
        : _symbol_table(std::make_unique<SymbolTable>()),
          _type_context(std::make_unique<TypeContext>()) {}

    ASTContext::~ASTContext() = default;
}