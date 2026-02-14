#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace CryoLSP
{

    /**
     * @brief Tracks open document contents
     *
     * Stores the latest content of each open document for full-sync mode.
     */
    class DocumentStore
    {
    public:
        DocumentStore() = default;

        // Document lifecycle
        void open(const std::string &uri, const std::string &content, int version);
        void update(const std::string &uri, const std::string &content, int version);
        void close(const std::string &uri);

        // Access
        std::optional<std::string> getContent(const std::string &uri) const;
        int getVersion(const std::string &uri) const;
        bool isOpen(const std::string &uri) const;

        // Get all open document URIs
        std::vector<std::string> getOpenDocuments() const;

    private:
        struct DocumentInfo
        {
            std::string content;
            int version = 0;
        };

        std::unordered_map<std::string, DocumentInfo> _documents;
    };

} // namespace CryoLSP
