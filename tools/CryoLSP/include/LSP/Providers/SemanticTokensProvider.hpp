#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"

namespace CryoLSP
{

    class SemanticTokensProvider
    {
    public:
        SemanticTokensProvider(AnalysisEngine &engine);

        SemanticTokens getSemanticTokens(const std::string &uri);

        // Return the legend for semantic tokens
        static SemanticTokensLegend getLegend();

    private:
        AnalysisEngine &_engine;
    };

} // namespace CryoLSP
