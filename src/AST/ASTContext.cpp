#include "AST/ASTContext.hpp"
#include "AST/ASTNode.hpp"

namespace Cryo
{
    ASTContext::ASTContext()
        : _type_arena(std::make_unique<TypeArena>()),
          _module_registry(std::make_unique<ModuleTypeRegistry>()),
          _symbol_table(std::make_unique<SymbolTable>(*_type_arena, *_module_registry)) {}

    ASTContext::~ASTContext() = default;
}
