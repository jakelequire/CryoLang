#include "../include/HoverProvider.hpp"

namespace CryoLSP
{

    HoverProvider::HoverProvider(Cryo::CompilerInstance *compiler, SymbolProvider *symbol_provider)
        : _compiler(compiler), _symbol_provider(symbol_provider)
    {
    }

    std::optional<Hover> HoverProvider::get_hover(const std::string &uri, const Position &position)
    {
        // Simple implementation - return empty
        // TODO: Implement hover functionality
        return std::nullopt;
    }

} // namespace CryoLSP