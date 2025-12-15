#pragma once

#include "LSPTypes.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>

namespace CryoLSP
{

    struct DocumentInfo
    {
        std::string uri;
        std::string language_id;
        int version;
        std::string content;
        std::chrono::steady_clock::time_point last_modified;
        bool needs_compilation = true;

        DocumentInfo(const std::string &uri_, const std::string &lang_id, int ver, const std::string &text)
            : uri(uri_), language_id(lang_id), version(ver), content(text),
              last_modified(std::chrono::steady_clock::now()) {}
    };

    /**
     * @brief Manages open documents and their state
     *
     * Handles document lifecycle, content synchronization, and change tracking
     * for efficient compilation and analysis.
     */
    class DocumentManager
    {
    public:
        DocumentManager() = default;
        ~DocumentManager() = default;

        // Document lifecycle
        bool open_document(const TextDocumentItem &document);
        bool close_document(const std::string &uri);
        bool update_document(const std::string &uri, int version,
                             const std::vector<TextDocumentContentChangeEvent> &changes);
        bool save_document(const std::string &uri);

        // Document access
        std::shared_ptr<DocumentInfo> get_document(const std::string &uri);
        std::vector<std::string> get_open_documents() const;
        bool is_document_open(const std::string &uri) const;

        // Content access
        std::string get_document_content(const std::string &uri);
        int get_document_version(const std::string &uri);

        // Change tracking
        bool mark_needs_compilation(const std::string &uri);
        bool needs_compilation(const std::string &uri);
        void mark_compiled(const std::string &uri);

        // Utility
        size_t document_count() const;
        void clear_all_documents();

    private:
        mutable std::mutex _documents_mutex;
        std::unordered_map<std::string, std::shared_ptr<DocumentInfo>> _documents;

        std::string apply_content_changes(const std::string &current_content,
                                          const std::vector<TextDocumentContentChangeEvent> &changes);
        std::string apply_incremental_change(const std::string &content,
                                             const TextDocumentContentChangeEvent &change);
        size_t position_to_offset(const std::string &content, const Position &position);
    };

} // namespace CryoLSP
