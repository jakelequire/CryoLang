#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include <vector>

namespace CryoLSP
{

    class SymbolProvider
    {
    public:
        SymbolProvider(AnalysisEngine &engine);

        std::vector<DocumentSymbol> getDocumentSymbols(const std::string &uri);

    private:
        AnalysisEngine &_engine;
    };

} // namespace CryoLSP
