#pragma once
/******************************************************************************
 * @file SymbolDumper.hpp
 * @brief Utility for dumping symbol tables to debug files
 *
 * Outputs ASCII-formatted symbol table information for each module,
 * useful for debugging symbol resolution and scope visibility.
 ******************************************************************************/

#include "Types/SymbolTable.hpp"
#include <string>
#include <ostream>

namespace Cryo
{
    // Forward declarations
    class ASTContext;

    /**
     * @brief Utility class for dumping symbol table information to files
     *
     * Generates debug files in the format {module}-symbols.dbg.txt containing
     * ASCII-formatted tables of all symbols visible in each module's scope.
     */
    class SymbolDumper
    {
    public:
        /**
         * @brief Dump symbol table for a module to a file
         * @param symbol_table The symbol table to dump
         * @param module_name Name of the module (used for filename)
         * @param output_dir Directory where debug files should be written
         * @return true if dump was successful, false otherwise
         */
        static bool dump_module_symbols(const SymbolTable &symbol_table,
                                        const std::string &module_name,
                                        const std::string &output_dir);

        /**
         * @brief Dump symbol table for all modules in an AST context
         * @param ast_context The AST context containing module information
         * @param symbol_table The symbol table to dump
         * @param output_dir Directory where debug files should be written
         * @return true if all dumps were successful, false otherwise
         */
        static bool dump_all_modules(const ASTContext &ast_context,
                                     const SymbolTable &symbol_table,
                                     const std::string &output_dir);

        /**
         * @brief Dump symbol table contents to an output stream
         * @param symbol_table The symbol table to dump
         * @param module_name Name of the module (for header)
         * @param os Output stream to write to
         */
        static void dump_to_stream(const SymbolTable &symbol_table,
                                   const std::string &module_name,
                                   std::ostream &os);

    private:
        /**
         * @brief Format a symbol for ASCII table output
         * @param symbol The symbol to format
         * @return Formatted string representation
         */
        static std::string format_symbol(const Symbol &symbol);

        /**
         * @brief Get a type description string from a TypeRef
         * @param type The type reference
         * @return Human-readable type description
         */
        static std::string get_type_description(TypeRef type);

        /**
         * @brief Calculate column widths for ASCII table formatting
         * @param symbols Vector of symbols to measure
         * @param name_width Output: maximum name width
         * @param kind_width Output: maximum kind width
         * @param type_width Output: maximum type width
         * @param scope_width Output: maximum scope width
         */
        static void calculate_column_widths(const std::vector<Symbol> &symbols,
                                            size_t &name_width,
                                            size_t &kind_width,
                                            size_t &type_width,
                                            size_t &scope_width);

        /**
         * @brief Write table header to stream
         * @param os Output stream
         * @param name_width Column width for name
         * @param kind_width Column width for kind
         * @param type_width Column width for type
         * @param scope_width Column width for scope
         */
        static void write_table_header(std::ostream &os,
                                       size_t name_width,
                                       size_t kind_width,
                                       size_t type_width,
                                       size_t scope_width);

        /**
         * @brief Write a horizontal separator line
         * @param os Output stream
         * @param name_width Column width for name
         * @param kind_width Column width for kind
         * @param type_width Column width for type
         * @param scope_width Column width for scope
         */
        static void write_separator(std::ostream &os,
                                    size_t name_width,
                                    size_t kind_width,
                                    size_t type_width,
                                    size_t scope_width);

        /**
         * @brief Write a symbol row to the table
         * @param os Output stream
         * @param symbol The symbol to write
         * @param name_width Column width for name
         * @param kind_width Column width for kind
         * @param type_width Column width for type
         * @param scope_width Column width for scope
         */
        static void write_symbol_row(std::ostream &os,
                                     const Symbol &symbol,
                                     size_t name_width,
                                     size_t kind_width,
                                     size_t type_width,
                                     size_t scope_width);
    };

} // namespace Cryo
