#include "DocumentManager.hpp"
#include <algorithm>
#include <sstream>

namespace CryoLSP
{

    bool DocumentManager::open_document(const TextDocumentItem &document)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto doc_info = std::make_shared<DocumentInfo>(document.uri, document.language_id, document.version, document.text);
        _documents[document.uri] = doc_info;

        return true;
    }

    bool DocumentManager::close_document(const std::string &uri)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto it = _documents.find(uri);
        if (it != _documents.end())
        {
            _documents.erase(it);
            return true;
        }

        return false;
    }

    bool DocumentManager::update_document(const std::string &uri, int version,
                                          const std::vector<TextDocumentContentChangeEvent> &changes)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto it = _documents.find(uri);
        if (it == _documents.end())
        {
            return false;
        }

        auto &doc = it->second;
        doc->version = version;
        doc->content = apply_content_changes(doc->content, changes);
        doc->last_modified = std::chrono::steady_clock::now();
        doc->needs_compilation = true;

        return true;
    }

    bool DocumentManager::save_document(const std::string &uri)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto it = _documents.find(uri);
        if (it != _documents.end())
        {
            it->second->needs_compilation = true;
            return true;
        }

        return false;
    }

    std::shared_ptr<DocumentInfo> DocumentManager::get_document(const std::string &uri)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto it = _documents.find(uri);
        if (it != _documents.end())
        {
            return it->second;
        }

        return nullptr;
    }

    std::vector<std::string> DocumentManager::get_open_documents() const
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        std::vector<std::string> documents;
        for (const auto &pair : _documents)
        {
            documents.push_back(pair.first);
        }

        return documents;
    }

    bool DocumentManager::is_document_open(const std::string &uri) const
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);
        return _documents.find(uri) != _documents.end();
    }

    std::string DocumentManager::get_document_content(const std::string &uri)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto it = _documents.find(uri);
        if (it != _documents.end())
        {
            return it->second->content;
        }

        return "";
    }

    int DocumentManager::get_document_version(const std::string &uri)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto it = _documents.find(uri);
        if (it != _documents.end())
        {
            return it->second->version;
        }

        return -1;
    }

    bool DocumentManager::mark_needs_compilation(const std::string &uri)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto it = _documents.find(uri);
        if (it != _documents.end())
        {
            it->second->needs_compilation = true;
            return true;
        }

        return false;
    }

    bool DocumentManager::needs_compilation(const std::string &uri)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto it = _documents.find(uri);
        if (it != _documents.end())
        {
            return it->second->needs_compilation;
        }

        return false;
    }

    void DocumentManager::mark_compiled(const std::string &uri)
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);

        auto it = _documents.find(uri);
        if (it != _documents.end())
        {
            it->second->needs_compilation = false;
        }
    }

    size_t DocumentManager::document_count() const
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);
        return _documents.size();
    }

    void DocumentManager::clear_all_documents()
    {
        std::lock_guard<std::mutex> lock(_documents_mutex);
        _documents.clear();
    }

    std::string DocumentManager::apply_content_changes(const std::string &current_content,
                                                       const std::vector<TextDocumentContentChangeEvent> &changes)
    {
        std::string result = current_content;

        for (const auto &change : changes)
        {
            if (!change.range)
            {
                // Full document replacement
                result = change.text;
            }
            else
            {
                // Incremental change
                result = apply_incremental_change(result, change);
            }
        }

        return result;
    }

    std::string DocumentManager::apply_incremental_change(const std::string &content,
                                                          const TextDocumentContentChangeEvent &change)
    {
        if (!change.range)
        {
            return change.text;
        }

        size_t start_offset = position_to_offset(content, change.range->start);
        size_t end_offset = position_to_offset(content, change.range->end);

        std::string result = content.substr(0, start_offset) + change.text + content.substr(end_offset);
        return result;
    }

    size_t DocumentManager::position_to_offset(const std::string &content, const Position &position)
    {
        size_t offset = 0;
        int current_line = 0;
        int current_char = 0;

        for (size_t i = 0; i < content.length(); ++i)
        {
            if (current_line == position.line && current_char == position.character)
            {
                return i;
            }

            if (content[i] == '\n')
            {
                current_line++;
                current_char = 0;
            }
            else
            {
                current_char++;
            }
        }

        return content.length();
    }

} // namespace CryoLSP
