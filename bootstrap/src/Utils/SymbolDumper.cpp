/******************************************************************************
 * @file SymbolDumper.cpp
 * @brief Implementation of SymbolDumper utility for debug file output
 ******************************************************************************/

#include "Utils/SymbolDumper.hpp"
#include "AST/ASTContext.hpp"
#include "Types/Type.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace Cryo
{
    // Minimum column widths for readability
    static constexpr size_t MIN_NAME_WIDTH = 20;
    static constexpr size_t MIN_KIND_WIDTH = 12;
    static constexpr size_t MIN_TYPE_WIDTH = 25;
    static constexpr size_t MIN_SCOPE_WIDTH = 15;
    static constexpr size_t MIN_VIS_WIDTH = 10;

    bool SymbolDumper::dump_module_symbols(const SymbolTable &symbol_table,
                                           const std::string &module_name,
                                           const std::string &output_dir)
    {
        // Ensure output directory exists
        std::filesystem::path dir_path(output_dir);
        if (!std::filesystem::exists(dir_path))
        {
            std::filesystem::create_directories(dir_path);
        }

        // Build output filename
        std::string safe_module_name = module_name;
        // Replace :: with _ for filesystem-safe name
        std::replace(safe_module_name.begin(), safe_module_name.end(), ':', '_');
        std::string filename = safe_module_name + "-symbols.dbg.txt";
        std::filesystem::path output_path = dir_path / filename;

        // Open output file
        std::ofstream file(output_path);
        if (!file.is_open())
        {
            return false;
        }

        dump_to_stream(symbol_table, module_name, file);
        file.close();
        return true;
    }

    bool SymbolDumper::dump_all_modules(const ASTContext &ast_context,
                                        const SymbolTable &symbol_table,
                                        const std::string &output_dir)
    {
        // For now, dump the main symbol table
        // Future: iterate through modules in the registry
        return dump_module_symbols(symbol_table, "main", output_dir);
    }

    void SymbolDumper::dump_to_stream(const SymbolTable &symbol_table,
                                      const std::string &module_name,
                                      std::ostream &os)
    {
        // Collect all symbols
        std::vector<Symbol> all_symbols = symbol_table.get_all_symbols_for_lsp();

        // Write header
        os << "================================================================================\n";
        os << "  Symbol Table Dump: " << module_name << "\n";
        os << "================================================================================\n";
        os << "  Module ID: " << symbol_table.current_module().id << "\n";
        os << "  Scope Depth: " << symbol_table.scope_depth() << "\n";
        os << "  Total Symbols: " << all_symbols.size() << "\n";
        os << "================================================================================\n\n";

        if (all_symbols.empty())
        {
            os << "  (No symbols in scope)\n";
            return;
        }

        // Calculate column widths
        size_t name_width, kind_width, type_width, scope_width;
        calculate_column_widths(all_symbols, name_width, kind_width, type_width, scope_width);

        // Write table
        write_separator(os, name_width, kind_width, type_width, scope_width);
        write_table_header(os, name_width, kind_width, type_width, scope_width);
        write_separator(os, name_width, kind_width, type_width, scope_width);

        // Group symbols by kind for readability
        std::vector<std::pair<SymbolKind, std::string>> kind_order = {
            {SymbolKind::Namespace, "Namespaces"},
            {SymbolKind::Type, "Types"},
            {SymbolKind::TypeAlias, "Type Aliases"},
            {SymbolKind::Function, "Functions"},
            {SymbolKind::Method, "Methods"},
            {SymbolKind::Intrinsic, "Intrinsics"},
            {SymbolKind::Variable, "Variables"},
            {SymbolKind::Constant, "Constants"},
            {SymbolKind::Parameter, "Parameters"},
            {SymbolKind::Field, "Fields"},
            {SymbolKind::EnumVariant, "Enum Variants"},
            {SymbolKind::GenericParam, "Generic Parameters"},
            {SymbolKind::Import, "Imports"},
        };

        for (const auto &[kind, section_name] : kind_order)
        {
            std::vector<const Symbol *> section_symbols;
            for (const auto &sym : all_symbols)
            {
                if (sym.kind == kind)
                {
                    section_symbols.push_back(&sym);
                }
            }

            if (section_symbols.empty())
                continue;

            // Sort symbols by name within section
            std::sort(section_symbols.begin(), section_symbols.end(),
                      [](const Symbol *a, const Symbol *b)
                      {
                          return a->name < b->name;
                      });

            // Write section
            for (const auto *sym : section_symbols)
            {
                write_symbol_row(os, *sym, name_width, kind_width, type_width, scope_width);
            }
        }

        write_separator(os, name_width, kind_width, type_width, scope_width);

        // Write summary by kind
        os << "\n";
        os << "Summary by Kind:\n";
        os << "----------------\n";
        for (const auto &[kind, section_name] : kind_order)
        {
            size_t count = 0;
            for (const auto &sym : all_symbols)
            {
                if (sym.kind == kind)
                    count++;
            }
            if (count > 0)
            {
                os << "  " << std::setw(20) << std::left << section_name << ": " << count << "\n";
            }
        }
    }

    std::string SymbolDumper::get_type_description(TypeRef type)
    {
        if (!type.is_valid())
        {
            return "<unresolved>";
        }

        const Type *t = type.get();
        if (!t)
        {
            return "<null>";
        }

        return t->display_name();
    }

    void SymbolDumper::calculate_column_widths(const std::vector<Symbol> &symbols,
                                               size_t &name_width,
                                               size_t &kind_width,
                                               size_t &type_width,
                                               size_t &scope_width)
    {
        name_width = MIN_NAME_WIDTH;
        kind_width = MIN_KIND_WIDTH;
        type_width = MIN_TYPE_WIDTH;
        scope_width = MIN_SCOPE_WIDTH;

        for (const auto &sym : symbols)
        {
            name_width = std::max(name_width, sym.name.length() + 2);
            kind_width = std::max(kind_width, symbol_kind_to_string(sym.kind).length() + 2);
            type_width = std::max(type_width, get_type_description(sym.type).length() + 2);
            scope_width = std::max(scope_width, sym.scope.length() + 2);
        }

        // Cap widths to prevent extremely wide output
        name_width = std::min(name_width, (size_t)40);
        type_width = std::min(type_width, (size_t)50);
        scope_width = std::min(scope_width, (size_t)30);
    }

    void SymbolDumper::write_table_header(std::ostream &os,
                                          size_t name_width,
                                          size_t kind_width,
                                          size_t type_width,
                                          size_t scope_width)
    {
        os << "| " << std::setw(name_width) << std::left << "Name"
           << "| " << std::setw(kind_width) << std::left << "Kind"
           << "| " << std::setw(type_width) << std::left << "Type"
           << "| " << std::setw(scope_width) << std::left << "Scope"
           << "| " << std::setw(MIN_VIS_WIDTH) << std::left << "Visibility"
           << "| Loc"
           << " |\n";
    }

    void SymbolDumper::write_separator(std::ostream &os,
                                       size_t name_width,
                                       size_t kind_width,
                                       size_t type_width,
                                       size_t scope_width)
    {
        os << "+-" << std::string(name_width, '-')
           << "+-" << std::string(kind_width, '-')
           << "+-" << std::string(type_width, '-')
           << "+-" << std::string(scope_width, '-')
           << "+-" << std::string(MIN_VIS_WIDTH, '-')
           << "+-" << std::string(12, '-')
           << "-+\n";
    }

    void SymbolDumper::write_symbol_row(std::ostream &os,
                                        const Symbol &symbol,
                                        size_t name_width,
                                        size_t kind_width,
                                        size_t type_width,
                                        size_t scope_width)
    {
        // Truncate strings if too long
        std::string name = symbol.name;
        if (name.length() > name_width - 1)
        {
            name = name.substr(0, name_width - 4) + "...";
        }

        std::string type_str = get_type_description(symbol.type);
        if (type_str.length() > type_width - 1)
        {
            type_str = type_str.substr(0, type_width - 4) + "...";
        }

        std::string scope = symbol.scope;
        if (scope.empty())
        {
            scope = "(global)";
        }
        if (scope.length() > scope_width - 1)
        {
            scope = scope.substr(0, scope_width - 4) + "...";
        }

        // Format location
        std::ostringstream loc_ss;
        loc_ss << symbol.location.line() << ":" << symbol.location.column();
        std::string loc_str = loc_ss.str();

        os << "| " << std::setw(name_width) << std::left << name
           << "| " << std::setw(kind_width) << std::left << symbol_kind_to_string(symbol.kind)
           << "| " << std::setw(type_width) << std::left << type_str
           << "| " << std::setw(scope_width) << std::left << scope
           << "| " << std::setw(MIN_VIS_WIDTH) << std::left << visibility_to_string(symbol.visibility)
           << "| " << std::setw(10) << std::left << loc_str
           << " |\n";
    }

    std::string SymbolDumper::format_symbol(const Symbol &symbol)
    {
        std::ostringstream oss;
        oss << symbol.name << " : " << get_type_description(symbol.type)
            << " (" << symbol_kind_to_string(symbol.kind) << ")";
        return oss.str();
    }

} // namespace Cryo
