#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include "LSP/DocumentStore.hpp"

#include <string>
#include <vector>

namespace CryoLSP
{

    enum class CompletionContext
    {
        General,         // Normal completion (top of scope, expression, etc.)
        MemberAccess,    // After '.' — fields + instance methods
        ScopeResolution, // After '::' — static methods, enum variants
    };

    class CompletionProvider
    {
    public:
        CompletionProvider(AnalysisEngine &engine, DocumentStore &documents);

        CompletionList getCompletions(const std::string &uri, const Position &position,
                                      const std::string &triggerCharacter = "");

    private:
        AnalysisEngine &_engine;
        DocumentStore &_documents;

        // Context detection — analyzes line text to determine what kind of completion
        CompletionContext detectContext(const std::string &uri, const Position &position,
                                       const std::string &triggerCharacter,
                                       std::string &contextIdentifier);

        // Dot-completion: fields + instance methods for a type
        void getMemberCompletions(const std::string &typeName, const std::string &uri,
                                  std::vector<CompletionItem> &items);

        // Scope resolution: static methods, enum variants
        void getScopeCompletions(const std::string &typeName, const std::string &uri,
                                 std::vector<CompletionItem> &items);

        // General completion: scope-aware symbols + keywords + types with sorting
        void getGeneralCompletions(const std::string &uri, const Position &position,
                                   std::vector<CompletionItem> &items);

        // Build a snippet insertText for a function/method
        std::string buildFunctionSnippet(const std::string &name,
                                         const std::vector<std::string> &paramNames);

        // Add keyword completions
        void addKeywords(std::vector<CompletionItem> &items);

        // Add type name completions
        void addBuiltinTypes(std::vector<CompletionItem> &items);
    };

} // namespace CryoLSP
