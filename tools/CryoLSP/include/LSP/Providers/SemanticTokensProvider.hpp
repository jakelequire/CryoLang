#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include "LSP/DocumentStore.hpp"

namespace CryoLSP
{

    class SemanticTokensProvider
    {
    public:
        SemanticTokensProvider(AnalysisEngine &engine, DocumentStore &documents);

        SemanticTokens getSemanticTokens(const std::string &uri);

        // Return the legend for semantic tokens
        static SemanticTokensLegend getLegend();

    private:
        AnalysisEngine &_engine;
        DocumentStore &_documents;
    };

} // namespace CryoLSP
