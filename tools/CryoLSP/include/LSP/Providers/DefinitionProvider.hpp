#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include "AST/ASTNode.hpp"
#include <optional>
#include <string>

namespace Cryo
{
    class CompilerInstance;
}

namespace CryoLSP
{

    class DefinitionProvider
    {
    public:
        DefinitionProvider(AnalysisEngine &engine);

        std::optional<Location> getDefinition(const std::string &uri, const Position &position);

    private:
        AnalysisEngine &_engine;

        // Build Location from an AST node (prefers name_location for declarations)
        std::optional<Location> buildLocationFromNode(
            Cryo::ASTNode *node, const std::string &name, const std::string &file_path);

        // Search for a declaration by name: current file -> imported modules -> intrinsics
        std::optional<Location> searchDeclaration(
            const std::string &name, Cryo::CompilerInstance *instance, const std::string &current_file);

        // Resolve a module name to its filesystem path
        std::string resolveModuleFilePath(
            const std::string &module_name, Cryo::CompilerInstance *instance);
    };

} // namespace CryoLSP
