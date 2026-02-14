#include "LSP/Providers/DiagnosticsProvider.hpp"

namespace CryoLSP
{

    DiagnosticsProvider::DiagnosticsProvider(AnalysisEngine &engine, Transport &transport)
        : _engine(engine), _transport(transport) {}

    void DiagnosticsProvider::publishDiagnostics(const std::string &uri, const std::string &content)
    {
        std::string file_path = uri_to_path(uri);
        auto result = _engine.analyzeDocument(file_path, content);

        // Build diagnostics array
        cjson::JsonArray diags;
        for (const auto &d : result.diagnostics)
        {
            diags.push_back(diagnostic_to_json(d));
        }

        // Publish
        cjson::JsonObject params;
        params["uri"] = cjson::JsonValue(uri);
        params["diagnostics"] = cjson::JsonValue(std::move(diags));

        _transport.sendNotification("textDocument/publishDiagnostics", cjson::JsonValue(std::move(params)));
    }

    void DiagnosticsProvider::clearDiagnostics(const std::string &uri)
    {
        cjson::JsonObject params;
        params["uri"] = cjson::JsonValue(uri);
        params["diagnostics"] = cjson::JsonValue(cjson::JsonArray{});

        _transport.sendNotification("textDocument/publishDiagnostics", cjson::JsonValue(std::move(params)));
    }

} // namespace CryoLSP
