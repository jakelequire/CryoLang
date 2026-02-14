#include "LSP/DocumentStore.hpp"

namespace CryoLSP
{

    void DocumentStore::open(const std::string &uri, const std::string &content, int version)
    {
        _documents[uri] = {content, version};
    }

    void DocumentStore::update(const std::string &uri, const std::string &content, int version)
    {
        _documents[uri] = {content, version};
    }

    void DocumentStore::close(const std::string &uri)
    {
        _documents.erase(uri);
    }

    std::optional<std::string> DocumentStore::getContent(const std::string &uri) const
    {
        auto it = _documents.find(uri);
        if (it != _documents.end())
            return it->second.content;
        return std::nullopt;
    }

    int DocumentStore::getVersion(const std::string &uri) const
    {
        auto it = _documents.find(uri);
        if (it != _documents.end())
            return it->second.version;
        return -1;
    }

    bool DocumentStore::isOpen(const std::string &uri) const
    {
        return _documents.find(uri) != _documents.end();
    }

    std::vector<std::string> DocumentStore::getOpenDocuments() const
    {
        std::vector<std::string> uris;
        uris.reserve(_documents.size());
        for (const auto &[uri, _] : _documents)
        {
            uris.push_back(uri);
        }
        return uris;
    }

} // namespace CryoLSP
