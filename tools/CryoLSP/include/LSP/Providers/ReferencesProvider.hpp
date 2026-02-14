#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include <vector>

namespace CryoLSP
{

    class ReferencesProvider
    {
    public:
        ReferencesProvider(AnalysisEngine &engine);

        std::vector<Location> getReferences(const std::string &uri, const Position &position, bool includeDeclaration);

    private:
        AnalysisEngine &_engine;
    };

} // namespace CryoLSP
