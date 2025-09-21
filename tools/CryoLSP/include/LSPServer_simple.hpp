#pragma once

#include "Compiler/CompilerInstance.hpp"
#include <memory>
#include <string>

namespace Cryo::LSP
{
    class LSPServer
    {
    public:
        LSPServer();
        ~LSPServer() = default;

        void run();

    private:
        std::unique_ptr<Cryo::CompilerInstance> _compiler;
        bool _initialized;
        bool _shutdown_requested;

        void process_message(const std::string& message);
        void send_response(const std::string& response);
    };
}
