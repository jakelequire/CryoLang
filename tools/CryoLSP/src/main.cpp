#include <iostream>
#include <memory>
#include <stdexcept>
#include <filesystem>

#include "Server.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Utils/Logger.hpp"

using namespace CryoLSP;

void print_usage()
{
    std::cout << "CryoLSP - Language Server Protocol implementation for CryoLang\n";
    std::cout << "Usage: cryolsp [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --help, -h          Show this help message\n";
    std::cout << "  --version, -v       Show version information\n";
    std::cout << "  --stdio             Use stdin/stdout for communication (default)\n";
    std::cout << "  --debug             Enable debug logging\n";
    std::cout << "  --log-file <file>   Write logs to specified file\n";
    std::cout << "\nThe server communicates via JSON-RPC over stdin/stdout.\n";
    std::cout << "This is typically used by editors and IDEs, not run directly.\n";
}

void print_version()
{
    std::cout << "CryoLSP v0.1.0\n";
    std::cout << "Language Server Protocol implementation for CryoLang\n";
    std::cout << "Built with CryoLang Compiler v0.1.0\n";
}

int main(int argc, char *argv[])
{
    try
    {
        // Always output to stderr first
        std::cerr << "[MAIN] CryoLSP starting with " << argc << " arguments:" << std::endl;
        for (int i = 0; i < argc; ++i)
        {
            std::cerr << "[MAIN] argv[" << i << "]: " << argv[i] << std::endl;
        }

        bool debug_mode = false;
        bool stdio_mode = false; // Default to TCP mode now
        bool tcp_mode = false;
        int tcp_port = 8080;
        std::string log_file;

        // Parse command line arguments
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];

            if (arg == "--help" || arg == "-h")
            {
                print_usage();
                return 0;
            }
            else if (arg == "--version" || arg == "-v")
            {
                print_version();
                return 0;
            }
            else if (arg == "--stdio")
            {
                stdio_mode = true; // Explicitly enable stdio mode
            }
            else if (arg == "--tcp")
            {
                tcp_mode = true;
                // Check if port is specified as next argument
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    tcp_port = std::stoi(argv[++i]);
                }
            }
            else if (arg == "--debug")
            {
                debug_mode = true;
            }
            else if (arg == "--log-file" && i + 1 < argc)
            {
                log_file = argv[++i];
            }
            else
            {
                std::cerr << "Unknown argument: " << arg << "\n";
                print_usage();
                return 1;
            }
        }

        // Default to TCP mode if no mode specified
        if (!stdio_mode && !tcp_mode)
        {
            tcp_mode = true;
        }

        // Initialize logging FIRST - before any other operations
        auto &logger = Cryo::Logger::instance();
        std::string logPath = "cryolsp.log";

        if (debug_mode || !log_file.empty())
        {
            if (!log_file.empty())
            {
                logPath = log_file;
            }

            // Always enable file logging for LSP to help with debugging
            logger.set_console_level(debug_mode ? Cryo::LogLevel::DEBUG : Cryo::LogLevel::INFO);
            logger.set_file_level(Cryo::LogLevel::DEBUG);
            logger.set_log_file(logPath);

            // Force immediate log initialization
            logger.info(Cryo::LogComponent::LSP, "=== CryoLSP Starting ====");
            logger.info(Cryo::LogComponent::LSP, "Debug logging enabled, log file: " + logPath);

            if (tcp_mode)
            {
                logger.info(Cryo::LogComponent::LSP, "Starting in TCP mode on port " + std::to_string(tcp_port));
            }
            else
            {
                logger.info(Cryo::LogComponent::LSP, "Starting in stdio mode");
            }
        }
        else
        {
            // Even without debug mode, enable basic file logging
            logger.set_file_level(Cryo::LogLevel::INFO);
            logger.set_log_file(logPath);
            logger.info(Cryo::LogComponent::LSP, "CryoLSP starting (basic logging enabled)");
        }

        // Create and configure the compiler instance
        auto compiler = new Cryo::CompilerInstance();
        compiler->set_lsp_mode(true);

        // Create and configure the LSP server
        Server lsp_server;
        lsp_server.set_compiler_instance(compiler);
        lsp_server.set_tcp_mode(tcp_mode, tcp_port);
        lsp_server.set_debug_mode(debug_mode);

        // Log server startup
        if (debug_mode)
        {
            Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Starting LSP server...");
        }

        // Run the server (this blocks until shutdown)
        lsp_server.run();

        if (debug_mode)
        {
            Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "LSP server shutdown complete");
        }

        // Clean up compiler instance
        delete compiler;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
}