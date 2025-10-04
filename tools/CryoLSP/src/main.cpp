#include "LanguageServer.hpp"
#include "Transport.hpp"
#include "Logger.hpp"
#include <iostream>
#include <memory>

using namespace Cryo::LSP;

int main(int argc, char* argv[]) {
    try {
        // Initialize logger
        Logger& logger = Logger::instance();
        logger.enable_debug_mode(true); // Enable debug logging
        logger.set_file_output("C:\\Programming\\apps\\CryoLang\\logs\\cryo-lsp.log"); // Write to log file
        logger.set_console_output(false); // Disable console output (interferes with LSP stdio)
        logger.info("LSP", "Starting CryoLSP Language Server");
        
        // Create transport (stdio by default)
        auto transport = std::make_unique<StdioTransport>();
        
        // Create and run the language server
        LanguageServer server(std::move(transport));
        
        logger.info("LSP", "Language server created, entering main loop");
        bool success = server.run();
        
        logger.info("LSP", "Language server exited " + std::string(success ? "successfully" : "with errors"));
        return success ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}