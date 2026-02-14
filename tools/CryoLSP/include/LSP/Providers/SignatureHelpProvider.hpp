#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include <optional>

namespace CryoLSP
{

    class SignatureHelpProvider
    {
    public:
        SignatureHelpProvider(AnalysisEngine &engine);

        std::optional<SignatureHelp> getSignatureHelp(const std::string &uri, const Position &position);

    private:
        AnalysisEngine &_engine;
    };

} // namespace CryoLSP
