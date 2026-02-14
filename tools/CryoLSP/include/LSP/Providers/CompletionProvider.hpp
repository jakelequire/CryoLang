#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"

namespace CryoLSP
{

    class CompletionProvider
    {
    public:
        CompletionProvider(AnalysisEngine &engine);

        CompletionList getCompletions(const std::string &uri, const Position &position);

    private:
        AnalysisEngine &_engine;

        // Add keyword completions
        void addKeywords(std::vector<CompletionItem> &items);

        // Add type name completions
        void addBuiltinTypes(std::vector<CompletionItem> &items);
    };

} // namespace CryoLSP
