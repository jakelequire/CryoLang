/******************************************************************************
 * @file ASTSpecializer.cpp
 * @brief Implementation of AST specialization for monomorphization
 ******************************************************************************/

#include "AST/ASTSpecializer.hpp"
#include "AST/ASTContext.hpp"
#include "Utils/Logger.hpp"

namespace Cryo
{

ASTSpecializer::ASTSpecializer(ASTContext &ctx, TypeArena &arena)
    : _ast_context(ctx), _arena(arena)
{
}

ASTNode *ASTSpecializer::specialize(const GenericTemplate &tmpl,
                                     const TypeSubstitution &subst,
                                     const std::string &specialized_name)
{
    // Check cache first
    if (has_specialization(specialized_name))
    {
        LOG_DEBUG(Cryo::LogComponent::GENERAL,
                  "ASTSpecializer: Using cached specialization for '{}'", specialized_name);
        return get_specialization(specialized_name);
    }

    // Get the original AST node from the template
    ASTNode *original = tmpl.ast_node;
    if (!original)
    {
        LOG_WARN(Cryo::LogComponent::GENERAL,
                 "ASTSpecializer: No AST node for template '{}', cannot specialize", tmpl.name);
        return nullptr;
    }

    LOG_DEBUG(Cryo::LogComponent::GENERAL,
              "ASTSpecializer: Specializing '{}' -> '{}'", tmpl.name, specialized_name);

    // Dispatch based on node type
    switch (original->kind())
    {
    case NodeKind::StructDeclaration:
    {
        auto *struct_decl = static_cast<StructDeclarationNode *>(original);
        return specialize_struct(struct_decl, tmpl, subst, specialized_name);
    }

    case NodeKind::ClassDeclaration:
    {
        auto *class_decl = static_cast<ClassDeclarationNode *>(original);
        return specialize_class(class_decl, tmpl, subst, specialized_name);
    }

    case NodeKind::EnumDeclaration:
    {
        auto *enum_decl = static_cast<EnumDeclarationNode *>(original);
        return specialize_enum(enum_decl, tmpl, subst, specialized_name);
    }

    default:
        LOG_WARN(Cryo::LogComponent::GENERAL,
                 "ASTSpecializer: Unsupported node kind {} for specialization",
                 NodeKindToString(original->kind()));
        return nullptr;
    }
}

ASTNode *ASTSpecializer::get_specialization(const std::string &specialized_name) const
{
    auto it = _specialization_cache.find(specialized_name);
    if (it != _specialization_cache.end())
    {
        return it->second;
    }
    return nullptr;
}

bool ASTSpecializer::has_specialization(const std::string &specialized_name) const
{
    return _specialization_cache.find(specialized_name) != _specialization_cache.end();
}

void ASTSpecializer::clear()
{
    _specialization_cache.clear();
    _owned_nodes.clear();
}

//=============================================================================
// Struct specialization
//=============================================================================

StructDeclarationNode *ASTSpecializer::specialize_struct(StructDeclarationNode *original,
                                                          const GenericTemplate &tmpl,
                                                          const TypeSubstitution &subst,
                                                          const std::string &specialized_name)
{
    LOG_DEBUG(Cryo::LogComponent::GENERAL,
              "ASTSpecializer::specialize_struct: Specializing '{}' -> '{}' with {} type params",
              original->name(), specialized_name, tmpl.params.size());

    // Log the type parameter mappings
    for (const auto &param : tmpl.params)
    {
        auto concrete = subst.get(param.type);
        LOG_DEBUG(Cryo::LogComponent::GENERAL,
                  "ASTSpecializer::specialize_struct: Param '{}' (TypeID={}) -> {}",
                  param.name, param.type.is_valid() ? param.type.id().id : 0,
                  concrete ? (*concrete)->display_name() : "<not found>");
    }

    // Clone the entire struct declaration
    ASTCloner cloner;
    auto cloned = cloner.clone<StructDeclarationNode>(original);

    if (!cloned)
    {
        LOG_ERROR(Cryo::LogComponent::GENERAL,
                  "ASTSpecializer: Failed to clone struct '{}'", original->name());
        return nullptr;
    }

    // Substitute types throughout the cloned AST
    // Use the template-aware constructor to build name-to-type map (T -> Token, etc.)
    ASTTypeSubstituter substituter(subst, _arena, tmpl);
    substituter.substitute(cloned.get());

    // The specialized struct no longer has generic parameters
    // (They've been substituted with concrete types)
    // Note: We don't clear generic_parameters() because we don't have a setter
    // The cloned struct still has them but the types are now concrete

    LOG_DEBUG(Cryo::LogComponent::GENERAL,
              "ASTSpecializer: Created specialized struct '{}' from '{}' with {} fields, {} methods",
              specialized_name, original->name(),
              cloned->fields().size(), cloned->methods().size());

    // Store and return
    return store_node(std::move(cloned), specialized_name);
}

//=============================================================================
// Class specialization
//=============================================================================

ClassDeclarationNode *ASTSpecializer::specialize_class(ClassDeclarationNode *original,
                                                        const GenericTemplate &tmpl,
                                                        const TypeSubstitution &subst,
                                                        const std::string &specialized_name)
{
    // Clone the entire class declaration
    ASTCloner cloner;
    auto cloned = cloner.clone<ClassDeclarationNode>(original);

    if (!cloned)
    {
        LOG_ERROR(Cryo::LogComponent::GENERAL,
                  "ASTSpecializer: Failed to clone class '{}'", original->name());
        return nullptr;
    }

    // Substitute types throughout the cloned AST
    // Use the template-aware constructor to build name-to-type map (T -> Token, etc.)
    ASTTypeSubstituter substituter(subst, _arena, tmpl);
    substituter.substitute(cloned.get());

    LOG_DEBUG(Cryo::LogComponent::GENERAL,
              "ASTSpecializer: Created specialized class '{}' from '{}' with {} fields, {} methods",
              specialized_name, original->name(),
              cloned->fields().size(), cloned->methods().size());

    // Store and return
    return store_node(std::move(cloned), specialized_name);
}

//=============================================================================
// Enum specialization
//=============================================================================

EnumDeclarationNode *ASTSpecializer::specialize_enum(EnumDeclarationNode *original,
                                                      const GenericTemplate &tmpl,
                                                      const TypeSubstitution &subst,
                                                      const std::string &specialized_name)
{
    // Clone the entire enum declaration
    ASTCloner cloner;
    auto cloned = cloner.clone<EnumDeclarationNode>(original);

    if (!cloned)
    {
        LOG_ERROR(Cryo::LogComponent::GENERAL,
                  "ASTSpecializer: Failed to clone enum '{}'", original->name());
        return nullptr;
    }

    // Substitute types throughout the cloned AST
    // Use the template-aware constructor to build name-to-type map (T -> Token, etc.)
    ASTTypeSubstituter substituter(subst, _arena, tmpl);
    substituter.substitute(cloned.get());

    LOG_DEBUG(Cryo::LogComponent::GENERAL,
              "ASTSpecializer: Created specialized enum '{}' from '{}' with {} variants",
              specialized_name, original->name(), cloned->variants().size());

    // Store and return
    return store_node(std::move(cloned), specialized_name);
}

//=============================================================================
// Method specialization (for individual methods if needed)
//=============================================================================

std::unique_ptr<StructMethodNode> ASTSpecializer::specialize_method(StructMethodNode *original,
                                                                     const TypeSubstitution &subst)
{
    // Clone the method
    ASTCloner cloner;
    auto cloned = cloner.clone<StructMethodNode>(original);

    if (!cloned)
    {
        LOG_ERROR(Cryo::LogComponent::GENERAL,
                  "ASTSpecializer: Failed to clone method '{}'", original->name());
        return nullptr;
    }

    // Substitute types
    ASTTypeSubstituter substituter(subst, _arena);
    substituter.substitute(cloned.get());

    return cloned;
}

} // namespace Cryo
