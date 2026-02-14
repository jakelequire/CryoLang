#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include <optional>

namespace CryoLSP
{

    class DefinitionProvider
    {
    public:
        DefinitionProvider(AnalysisEngine &engine);

        std::optional<Location> getDefinition(const std::string &uri, const Position &position);

    private:
        AnalysisEngine &_engine;
    };

} // namespace CryoLSP
