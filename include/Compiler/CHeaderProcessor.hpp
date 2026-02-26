#pragma once

#include "Compiler/CParser.hpp"
#include "AST/ASTNode.hpp"
#include "Types/TypeID.hpp"

#include <string>
#include <vector>
#include <memory>

namespace Cryo
{
    class TypeArena;

    /**
     * @brief Orchestrates the C header import pipeline:
     *   1. Resolves include path relative to source directory
     *   2. Runs clang-20 -E -P to preprocess
     *   3. Parses preprocessed C with CParser
     *   4. Maps C types to Cryo TypeRefs
     *   5. Creates FunctionDeclarationNode AST nodes
     */
    class CHeaderProcessor
    {
    public:
        /**
         * @brief Process a single C header file.
         * @param include_path Path from the #include directive (relative or absolute)
         * @param source_dir Directory of the .cryo source file (for resolving relative paths)
         * @param arena TypeArena for type mapping
         * @return Vector of FunctionDeclarationNode ready to inject into ExternBlockNode
         */
        std::vector<std::unique_ptr<FunctionDeclarationNode>> process_header(
            const std::string &include_path,
            const std::string &source_dir,
            TypeArena &arena);

        /**
         * @brief Check if a matching .c file exists for linking.
         * @param header_path Resolved absolute path to header file
         * @return Path to .c file if found, empty string otherwise
         */
        static std::string find_matching_c_file(const std::string &header_path);

    private:
        /**
         * @brief Run clang-20 -E -P on a header file.
         * @param resolved_path Absolute path to the header
         * @return Preprocessed C text, or empty on failure
         */
        std::string run_preprocessor(const std::string &resolved_path);

        /**
         * @brief Map a C type string to a Cryo TypeRef.
         */
        TypeRef map_c_type(const std::string &c_type, TypeArena &arena);

        /**
         * @brief Create a FunctionDeclarationNode from a CFunctionDecl.
         */
        std::unique_ptr<FunctionDeclarationNode> create_function_node(
            const CFunctionDecl &decl,
            TypeArena &arena);
    };

} // namespace Cryo
