#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include "LSP/Transport.hpp"
#include <string>
#include <vector>

namespace CryoLSP
{

    class DiagnosticsProvider
    {
    public:
        DiagnosticsProvider(AnalysisEngine &engine, Transport &transport);

        // Analyze a document and publish diagnostics
        void publishDiagnostics(const std::string &uri, const std::string &content);

        // Clear diagnostics for a document
        void clearDiagnostics(const std::string &uri);

    private:
        AnalysisEngine &_engine;
        Transport &_transport;
    };

} // namespace CryoLSP
