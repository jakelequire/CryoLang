#include "LSPServer.hpp"
#include "Utils/Logger.hpp"
#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace CryoLSP;

void print_usage(const char *program_name)
{
    std::cout << "CryoLang Language Server Protocol (LSP) Server\n";
    std::cout << "\nUsage: " << program_name << " [options]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --port PORT        TCP port to listen on (default: 7777)\n";
    std::cout << "  --host HOST        Host address to bind to (default: localhost)\n";
    std::cout << "  --log-level LEVEL  Log level: debug, info, warn, error (default: info)\n";
    std::cout << "  --log-file FILE    Log file path (default: stderr)\n";
    std::cout << "  --config FILE      Configuration file path\n";
    std::cout << "  --help, -h         Show this help message\n";
    std::cout << "  --version, -v      Show version information\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program_name << " --port 8080\n";
    std::cout << "  " << program_name << " --host 0.0.0.0 --port 7777 --log-level debug\n";
    std::cout << "\nFor more information, visit: https://github.com/YourOrg/CryoLang\n";
}

void print_version()
{
    std::cout << "CryoLSP Version 1.0.0\n";
    std::cout << "CryoLang Language Server Protocol Implementation\n";
    std::cout << "Built with TCP communication support\n";
}

struct CommandLineArgs
{
    int port = 7777;
    std::string host = "localhost";
    std::string log_level = "info";
    std::string log_file = "";
    std::string config_file = "";
    bool show_help = false;
    bool show_version = false;
};

CommandLineArgs parse_command_line(int argc, char *argv[])
{
    CommandLineArgs args;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            args.show_help = true;
        }
        else if (arg == "--version" || arg == "-v")
        {
            args.show_version = true;
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            args.port = std::atoi(argv[++i]);
            if (args.port <= 0 || args.port > 65535)
            {
                std::cerr << "Error: Invalid port number. Must be between 1 and 65535.\n";
                std::exit(1);
            }
        }
        else if (arg == "--host" && i + 1 < argc)
        {
            args.host = argv[++i];
        }
        else if (arg == "--log-level" && i + 1 < argc)
        {
            args.log_level = argv[++i];
        }
        else if (arg == "--log-file" && i + 1 < argc)
        {
            args.log_file = argv[++i];
        }
        else if (arg == "--config" && i + 1 < argc)
        {
            args.config_file = argv[++i];
        }
        else
        {
            std::cerr << "Error: Unknown argument: " << arg << "\n";
            std::cerr << "Use --help for usage information.\n";
            std::exit(1);
        }
    }

    return args;
}

void setup_logging(const CommandLineArgs &args)
{
    std::cout << "Configuring log level: " << args.log_level << std::endl;

    // Configure logging based on command line arguments
    Cryo::LogLevel level = Cryo::LogLevel::INFO;

    if (args.log_level == "debug")
    {
        level = Cryo::LogLevel::DEBUG;
    }
    else if (args.log_level == "info")
    {
        level = Cryo::LogLevel::INFO;
    }
    else if (args.log_level == "warn")
    {
        level = Cryo::LogLevel::WARN;
    }
    else if (args.log_level == "error")
    {
        level = Cryo::LogLevel::FATAL;
    }
    else
    {
        std::cerr << "Warning: Unknown log level '" << args.log_level << "', using 'info'\n";
    }

    std::cout << "Getting logger instance..." << std::endl;

    try
    {
        // Get logger instance and configure it
        auto &logger = Cryo::Logger::instance();

        std::cout << "Creating logger config..." << std::endl;
        // Create logger config and initialize
        auto config = Cryo::Logger::create_default_config();
        config.console_level = level;
        config.file_level = Cryo::LogLevel::DEBUG; // Always log debug level to file
        config.enable_colors = true;
        config.enable_timestamps = true;

        // Always ensure we have a log file for debugging
        if (args.log_file.empty())
        {
            // Use the logs folder if CRYO_SRC environment variable is available
            const char *cryo_src = std::getenv("CRYO_SRC");
            if (cryo_src != nullptr)
            {
                config.log_file_path = std::string(cryo_src) + "/logs/cryo-lsp-debug.log";
            }
            else
            {
                // Fallback to current directory
                config.log_file_path = "cryo-lsp-debug.log";
            }
        }
        else
        {
            config.log_file_path = args.log_file;
        }

        std::cout << "Log file path: " << config.log_file_path << std::endl;
        std::cout << "Initializing logger with config..." << std::endl;

        // Try to initialize logger
        logger.initialize(config);

        std::cout << "Logger initialized successfully!" << std::endl;
        logger.info(Cryo::LogComponent::LSP, "CryoLSP starting up...");
        logger.info(Cryo::LogComponent::LSP, "Log level: " + args.log_level);
        logger.info(Cryo::LogComponent::LSP, "Logging to file: " + config.log_file_path);
        logger.debug(Cryo::LogComponent::LSP, "Debug logging enabled");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to initialize logging: " << e.what() << std::endl;
        std::cout << "Continuing without advanced logging..." << std::endl;
        // Continue without file logging if it fails
    }
}

LSPServer::Config create_server_config(const CommandLineArgs &args)
{
    LSPServer::Config config;

    config.port = args.port;
    config.host = args.host;
    config.enable_debug_logging = (args.log_level == "debug");
    config.enable_trace_logging = (args.log_level == "debug");
    config.enable_diagnostics = true;
    config.enable_completion = true;
    config.enable_hover = true;
    config.enable_goto_definition = true;

    // Load additional configuration from file if specified
    if (!args.config_file.empty())
    {
        // TODO: Implement configuration file loading
        auto &logger = Cryo::Logger::instance();
        logger.info(Cryo::LogComponent::LSP, "Loading configuration from: " + args.config_file);
    }

    return config;
}

void handle_shutdown_signals()
{
    // Set up signal handlers for graceful shutdown
    // This would typically involve signal() or sigaction() calls
    // For now, we'll rely on the server's built-in shutdown handling
}

int main(int argc, char *argv[])
{
    try
    {
        std::cout << "CryoLSP starting..." << std::endl;

        // Parse command line arguments
        CommandLineArgs args = parse_command_line(argc, argv);

        // Handle help and version requests
        if (args.show_help)
        {
            print_usage(argv[0]);
            return 0;
        }

        if (args.show_version)
        {
            print_version();
            return 0;
        }

        std::cout << "Setting up logging..." << std::endl;
        // Setup logging
        setup_logging(args);

        // Setup signal handling for graceful shutdown
        handle_shutdown_signals();

        std::cout << "Creating server configuration..." << std::endl;
        // Create server configuration
        LSPServer::Config config = create_server_config(args);

        std::cout << "Starting CryoLSP server on " << args.host << ":" << args.port << std::endl;

        // Log startup information
        auto &logger = Cryo::Logger::instance();
        logger.info(Cryo::LogComponent::LSP, "Starting CryoLSP server on " + args.host + ":" + std::to_string(args.port));
#ifdef _WIN32
        logger.info(Cryo::LogComponent::LSP, "Process ID: " + std::to_string(GetCurrentProcessId()));
#else
        logger.info(Cryo::LogComponent::LSP, "Process ID: " + std::to_string(getpid()));
#endif

        std::cout << "Creating LSP server instance..." << std::endl;
        // Create and start the LSP server
        logger.debug(Cryo::LogComponent::LSP, "Creating LSP server instance...");
        LSPServer server(config);

        std::cout << "Initializing LSP server..." << std::endl;
        if (!server.initialize())
        {
            std::cerr << "Failed to initialize LSP server" << std::endl;
            logger.error(Cryo::LogComponent::LSP, "Failed to initialize LSP server");
            return 1;
        }

        std::cout << "Starting LSP server..." << std::endl;
        logger.debug(Cryo::LogComponent::LSP, "Starting LSP server...");
        if (!server.start())
        {
            std::cerr << "Failed to start LSP server" << std::endl;
            logger.error(Cryo::LogComponent::LSP, "Failed to start LSP server");
            logger.error(Cryo::LogComponent::LSP, "Server configuration - Host: " + config.host + ", Port: " + std::to_string(config.port));
            return 1;
        }

        std::cout << "LSP server started successfully on port " << args.port << std::endl;
        logger.info(Cryo::LogComponent::LSP, "LSP server started successfully");
        logger.info(Cryo::LogComponent::LSP, "Ready to accept client connections...");

        // Keep server running (this will block until shutdown signal)
        // Wait for shutdown signal - in a real implementation, this would wait for SIGTERM/SIGINT
        while (server.is_running())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        logger.info(Cryo::LogComponent::LSP, "LSP server shutting down...");

        // Cleanup
        server.stop();

        logger.info(Cryo::LogComponent::LSP, "LSP server stopped successfully");

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        auto &logger = Cryo::Logger::instance();
        logger.error(Cryo::LogComponent::LSP, "Fatal error: " + std::string(e.what()));
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown fatal error occurred\n";
        auto &logger = Cryo::Logger::instance();
        logger.error(Cryo::LogComponent::LSP, "Unknown fatal error occurred");
        return 1;
    }
}
