#pragma once
/******************************************************************************
 * @file ASTSpecializer.hpp
 * @brief AST specialization for generic type monomorphization
 *
 * ASTSpecializer creates specialized versions of generic type AST nodes
 * by cloning the original AST and substituting type parameters with
 * concrete types. This enables proper codegen for generic types.
 ******************************************************************************/

#include "AST/ASTNode.hpp"
#include "AST/ASTCloner.hpp"
#include "AST/ASTTypeSubstituter.hpp"
#include "Types/GenericRegistry.hpp"
#include "Types/TypeArena.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace Cryo
{
    // Forward declarations
    class ASTContext;

    /**************************************************************************
     * @brief Specializes generic type ASTs for monomorphization
     *
     * When the Monomorphizer processes a generic type instantiation (e.g.,
     * Array<Token>), it calls the ASTSpecializer to create a specialized
     * AST with all type parameters replaced by concrete types.
     *
     * Usage:
     *   ASTSpecializer specializer(ast_context, arena);
     *   ASTNode* specialized = specializer.specialize(template, subst, "Array_Token");
     **************************************************************************/
    class ASTSpecializer
    {
    private:
        ASTContext &_ast_context;
        TypeArena &_arena;

        // Cache of specialized AST nodes to avoid re-specializing
        std::unordered_map<std::string, ASTNode *> _specialization_cache;

        // Owns specialized AST nodes to ensure they outlive usage
        std::vector<std::unique_ptr<ASTNode>> _owned_nodes;

    public:
        ASTSpecializer(ASTContext &ctx, TypeArena &arena);
        ~ASTSpecializer() = default;

        // Non-copyable
        ASTSpecializer(const ASTSpecializer &) = delete;
        ASTSpecializer &operator=(const ASTSpecializer &) = delete;

        /**
         * @brief Specialize a generic template with given type substitutions
         *
         * This is the callback signature expected by Monomorphizer.
         *
         * @param tmpl The generic template to specialize
         * @param subst Type substitution map (T -> Token, etc.)
         * @param specialized_name Mangled name for the specialized type
         * @return Pointer to the specialized AST node
         */
        ASTNode *specialize(const GenericTemplate &tmpl,
                            const TypeSubstitution &subst,
                            const std::string &specialized_name);

        /**
         * @brief Get a previously specialized AST node by name
         * @param specialized_name The mangled name
         * @return Pointer to specialized node, or nullptr if not found
         */
        ASTNode *get_specialization(const std::string &specialized_name) const;

        /**
         * @brief Check if a specialization exists
         */
        bool has_specialization(const std::string &specialized_name) const;

        /**
         * @brief Clear all cached specializations
         */
        void clear();

        /**
         * @brief Get count of specialized nodes
         */
        size_t specialization_count() const { return _specialization_cache.size(); }

    private:
        /**
         * @brief Specialize a struct declaration
         */
        StructDeclarationNode *specialize_struct(StructDeclarationNode *original,
                                                  const GenericTemplate &tmpl,
                                                  const TypeSubstitution &subst,
                                                  const std::string &specialized_name);

        /**
         * @brief Specialize a class declaration
         */
        ClassDeclarationNode *specialize_class(ClassDeclarationNode *original,
                                                const GenericTemplate &tmpl,
                                                const TypeSubstitution &subst,
                                                const std::string &specialized_name);

        /**
         * @brief Specialize an enum declaration
         */
        EnumDeclarationNode *specialize_enum(EnumDeclarationNode *original,
                                              const GenericTemplate &tmpl,
                                              const TypeSubstitution &subst,
                                              const std::string &specialized_name);

        /**
         * @brief Specialize a method within a specialized type
         */
        std::unique_ptr<StructMethodNode> specialize_method(StructMethodNode *original,
                                                             const TypeSubstitution &subst);

        /**
         * @brief Store a specialized node and return raw pointer
         */
        template <typename T>
        T *store_node(std::unique_ptr<T> node, const std::string &name)
        {
            T *ptr = node.get();
            _owned_nodes.push_back(std::move(node));
            _specialization_cache[name] = ptr;
            return ptr;
        }
    };

} // namespace Cryo
