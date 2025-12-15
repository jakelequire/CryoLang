#pragma once

#include "LSPTypes.hpp"
#include "SymbolProvider.hpp"
#include "Compiler/CompilerInstance.hpp"
#include <optional>
#include <string>

namespace CryoLSP
{

    /**
     * @brief Provides rich hover information with syntax highlighting
     *
     * Professional-grade hover tooltips with:
     * - Type information with syntax highlighting
     * - Function signatures and parameter details
     * - Documentation and usage examples
     * - Memory layout for structs/classes
     * - Compiler intrinsic information
     */
    class HoverProvider
    {
    public:
        HoverProvider(Cryo::CompilerInstance *compiler, SymbolProvider *symbol_provider);
        ~HoverProvider() = default;

        std::optional<Hover> get_hover(const std::string &uri, const Position &position);

    private:
        Cryo::CompilerInstance *_compiler;
        SymbolProvider *_symbol_provider;

        // Hover content generators
        std::string generate_function_hover(Cryo::FunctionDeclarationNode *func_node);
        std::string generate_struct_hover(Cryo::StructDeclarationNode *struct_node);
        std::string generate_class_hover(Cryo::ClassDeclarationNode *class_node);
        std::string generate_variable_hover(Cryo::VariableDeclarationNode *var_node);
        std::string generate_enum_hover(Cryo::EnumDeclarationNode *enum_node);
        std::string generate_type_hover(Cryo::Type *type);

        // Primitive and keyword information
        std::string generate_primitive_hover(const std::string &primitive_name);
        std::string generate_keyword_hover(const std::string &keyword);
        std::string generate_intrinsic_hover(const std::string &intrinsic_name);

        // Markdown formatting helpers
        std::string format_as_code_block(const std::string &code, const std::string &language = "cryo");
        std::string format_signature(const std::string &signature);
        std::string format_documentation(const std::string &docs);
        std::string format_type_info(const std::string &type_name, const std::string &details);

        // Syntax highlighting for tooltips
        std::string apply_syntax_highlighting(const std::string &code);
        std::string highlight_keywords(const std::string &text);
        std::string highlight_types(const std::string &text);
        std::string highlight_strings_and_numbers(const std::string &text);

        // Built-in information databases
        std::string get_primitive_documentation(const std::string &primitive);
        std::string get_keyword_documentation(const std::string &keyword);
        std::string get_intrinsic_documentation(const std::string &intrinsic);

        // Memory layout helpers
        std::string generate_memory_layout_info(Cryo::StructDeclarationNode *struct_node);
        size_t calculate_struct_size(Cryo::StructDeclarationNode *struct_node);
        size_t calculate_field_offset(Cryo::StructDeclarationNode *struct_node, const std::string &field_name);
    };

} // namespace CryoLSP
