#include "LanguageServer.hpp"
#include "Transport.hpp"
#include "TcpTransport.hpp"
#include "Logger.hpp"
#include "Utils/OS.hpp"
#include <iostream>
#include <memory>
#include <filesystem>
#include <cstdlib>

using namespace Cryo::LSP;

int main(int argc, char *argv[])
{
    try
    {
        // Initialize OS utility first
        if (!Cryo::Utils::OS::initialize(argc > 0 ? argv[0] : ""))
        {
            std::cerr << "Fatal error: Failed to initialize OS utility for LSP" << std::endl;
            return 1;
        }

        // Initialize logger with cross-platform log file path
        Logger &logger = Logger::instance();
        // Debug logging disabled to reduce log spam

        // Get logs directory from OS utility
        auto& os = Cryo::Utils::OS::instance();
        std::string log_file = os.join_path(os.get_logs_directory(), "cryo-lsp.log");

        logger.set_file_output(log_file); // Write to log file
        logger.set_console_output(false); // Disable console output (interferes with LSP stdio)
        logger.info("LSP", "==== CryoLSP Language Server Starting ====");
        logger.info("LSP", "Version: Debug Build");
        logger.info("LSP", "Log file: " + log_file);
        logger.info("LSP", "Command line args: " + std::to_string(argc));
        for (int i = 0; i < argc; i++) {
            logger.info("LSP", "  arg[" + std::to_string(i) + "]: " + std::string(argv[i]));
        }
        logger.info("LSP", "Working directory: " + os.get_working_directory());
        logger.info("LSP", "Waiting for VS Code initialize request...");

        // Debug: Create a file to prove the server process actually started
        try {
            // Debug file creation disabled to reduce log spam
        } catch (...) {
            logger.error("LSP", "Failed to create debug file");
        }

        // Create transport based on command line arguments
        auto config = TransportFactory::parse_config(argc, argv);
        auto transport = TransportFactory::create_from_config(config);

        if (config.type == TransportFactory::TransportConfig::Socket)
        {
            logger.info("LSP", "Using TCP socket transport on port " + std::to_string(config.socket_port));
        }
        else
        {
            logger.info("LSP", "Using stdio transport");
        }

        // Create and run the language server
        LanguageServer server(std::move(transport));

        logger.info("LSP", "Language server created, entering main loop");
        bool success = server.run();

        logger.info("LSP", "Language server exited " + std::string(success ? "successfully" : "with errors"));
        return success ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}