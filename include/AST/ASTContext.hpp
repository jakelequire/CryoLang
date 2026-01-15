#pragma once
#include <memory>
#include <vector>
#include <unordered_map>

#include "Types/TypeArena.hpp"
#include "Types/ModuleTypeRegistry.hpp"
#include "Types/SymbolTable.hpp"

namespace Cryo
{
    class ASTNode;

    class ASTContext
    {
    private:
        std::vector<std::unique_ptr<ASTNode>> _nodes;
        std::unique_ptr<TypeArena> _type_arena;
        std::unique_ptr<ModuleTypeRegistry> _module_registry;
        std::unique_ptr<SymbolTable> _symbol_table;

    public:
        ASTContext();
        ~ASTContext();

        // Memory management - keep template in header
        template <typename T, typename... Args>
        T *create_node(Args &&...args)
        {
            static_assert(std::is_base_of<ASTNode, T>::value, "T must derive from ASTNode");
            auto node = std::make_unique<T>(std::forward<Args>(args)...);
            T *ptr = node.get();
            _nodes.push_back(std::move(node));
            return ptr;
        }

        // Global state - new API
        TypeArena &types() { return *_type_arena; }
        ModuleTypeRegistry &modules() { return *_module_registry; }
        SymbolTable &symbols() { return *_symbol_table; }

        // Memory statistics
        size_t node_count() const { return _nodes.size(); }
    };
}