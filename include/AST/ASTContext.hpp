#pragma once
#include <memory>
#include <vector>
#include <unordered_map>

namespace Cryo
{
    class ASTNode;
    class SymbolTable;

    class ASTContext
    {
    private:
        std::vector<std::unique_ptr<ASTNode>> _nodes;
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

        // Global state
        SymbolTable &symbols() { return *_symbol_table; }

        // Memory statistics
        size_t node_count() const { return _nodes.size(); }
    };
}