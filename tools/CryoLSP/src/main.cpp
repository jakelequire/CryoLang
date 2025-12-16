#include "LSPServer.hpp"
#include "Utils/Logger.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <filesystem>
#include <csignal>
#include <ctime>
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
    std::cout << "  --config FILE      Configuration file path\n";
    std::cout << "  --help, -h         Show this help message\n";
    std::cout << "  --version, -v      Show version information\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program_name << " --port 8080\n";
    std::cout << "  " << program_name << " --host 0.0.0.0 --port 7777\n";
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

void setup_logging(std::ofstream &debug_log)
{
    debug_log << "[SETUP_LOGGING] Skipping logger initialization - known to crash" << std::endl;
    debug_log << "[SETUP_LOGGING] Logger functionality disabled but server will continue" << std::endl;
    debug_log.flush();
    // Note: Logger calls in main() will be ignored/commented out
        debug_log << "[SETUP_LOGGING] Got logger instance successfully" << std::endl;
        debug_log.flush();

        debug_log << "[SETUP_LOGGING] Creating logger config..." << std::endl;
        debug_log.flush();
        // Create logger config and initialize
        debug_log << "[SETUP_LOGGING] Calling create_default_config..." << std::endl;
        debug_log.flush();
        auto config = Cryo::Logger::create_default_config();
        debug_log << "[SETUP_LOGGING] Got default config successfully" << std::endl;
        debug_log.flush();
        
        config.console_level = level;
        config.file_level = Cryo::LogLevel::DEBUG; // Always log debug level to file
        config.enable_colors = true;
        config.enable_timestamps = true;

        // Always log to ./logs directory
        config.log_file_path = "./logs/cryo-lsp.log";

        debug_log << "[SETUP_LOGGING] Log file path: " << config.log_file_path << std::endl;
        debug_log.flush();
        
        // Check if log directory exists and try to create it
        debug_log << "[SETUP_LOGGING] Checking log directory..." << std::endl;
        debug_log.flush();
        
        std::filesystem::path log_path(config.log_file_path);
        std::filesystem::path log_dir = log_path.parent_path();
        
        if (!log_dir.empty()) {
            debug_log << "[SETUP_LOGGING] Log directory: " << log_dir << std::endl;
            debug_log.flush();
            if (!std::filesystem::exists(log_dir)) {
                debug_log << "[SETUP_LOGGING] Creating log directory..." << std::endl;
                debug_log.flush();
                try {
                    std::filesystem::create_directories(log_dir);
                    debug_log << "[SETUP_LOGGING] Log directory created successfully" << std::endl;
                    debug_log.flush();
                } catch (const std::exception& e) {
                    debug_log << "[SETUP_LOGGING] Warning: Could not create log directory: " << e.what() << std::endl;
                    debug_log.flush();
                }
            } else {
                debug_log << "[SETUP_LOGGING] Log directory already exists" << std::endl;
                debug_log.flush();
            }
        }
        
        debug_log << "[SETUP_LOGGING] About to initialize logger (using standard filesystem)..." << std::endl;
        debug_log.flush();

        // Initialize logger now that OS utility is ready
        logger.initialize(config);
        
        debug_log << "[SETUP_LOGGING] Logger initialized successfully!" << std::endl;
        debug_log.flush();
        
        debug_log << "[SETUP_LOGGING] Making logger method calls..." << std::endl;
        debug_log.flush();
        
        logger.info(Cryo::LogComponent::LSP, "CryoLSP starting up...");
        logger.info(Cryo::LogComponent::LSP, "Log level: debug");
        logger.info(Cryo::LogComponent::LSP, "Logging to file: " + config.log_file_path);
        logger.debug(Cryo::LogComponent::LSP, "Debug logging enabled");
        
        debug_log << "[SETUP_LOGGING] All logger method calls completed successfully" << std::endl;
        debug_log.flush();
    }
    catch (const std::exception &e)
    {
        debug_log << "[SETUP_LOGGING] EXCEPTION in setup_logging: " << e.what() << std::endl;
        debug_log.flush();
        // Continue without file logging if it fails
    }
    catch (...)
    {
        debug_log << "[SETUP_LOGGING] UNKNOWN EXCEPTION in setup_logging" << std::endl;
        debug_log.flush();
    }
    
    debug_log << "[SETUP_LOGGING] Exiting setup_logging function" << std::endl;
    debug_log.flush();
}

LSPServer::Config create_server_config(const CommandLineArgs &args)
{
    LSPServer::Config config;

    config.port = args.port;
    config.host = args.host;
    config.enable_debug_logging = true;
    config.enable_trace_logging = true;
    config.enable_diagnostics = true;
    config.enable_completion = true;
    config.enable_hover = true;
    config.enable_goto_definition = true;

    // Load additional configuration from file if specified
    if (!args.config_file.empty())
    {
        // TODO: Implement configuration file loading
        // Note: Logger disabled for testing - config file loading noted but not logged
    }

    return config;
}

LSPServer* g_server_instance = nullptr;

void signal_handler(int signal)
{
    std::cout << "\nReceived signal " << signal << ", shutting down gracefully..." << std::endl;
    if (g_server_instance) {
        g_server_instance->shutdown();
    }
    std::exit(0);
}

void handle_shutdown_signals()
{
    // Set up signal handlers for graceful shutdown
    std::cout << "Setting up signal handlers..." << std::endl;
    
#ifdef _WIN32
    // Windows signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#else
    // Unix/Linux signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGHUP, signal_handler);
    std::signal(SIGPIPE, SIG_IGN); // Ignore broken pipe
#endif
    
    std::cout << "Signal handlers configured" << std::endl;
}

int main(int argc, char *argv[])
{
    // IMMEDIATELY set up file logging before doing anything else
    std::string debug_log_path;
    const char *cryo_src = std::getenv("CRYO_SRC");
    if (cryo_src != nullptr) {
        debug_log_path = std::string(cryo_src) + "/logs/cryo-lsp-startup-debug.log";
    } else {
        debug_log_path = "/workspaces/CryoLang/logs/cryo-lsp-startup-debug.log";
    }
    
    // Create logs directory if it doesn't exist
    try {
        std::filesystem::path log_dir = std::filesystem::path(debug_log_path).parent_path();
        if (!std::filesystem::exists(log_dir)) {
            std::filesystem::create_directories(log_dir);
        }
    } catch (...) {
        // Fallback to current directory if we can't create logs dir
        debug_log_path = "./cryo-lsp-startup-debug.log";
    }
    
    // Open debug log file immediately
    std::ofstream debug_log(debug_log_path, std::ios::app);
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    debug_log << "\n=== LSP SERVER STARTUP DEBUG - " << std::ctime(&time_t) << " ===" << std::endl;
    debug_log << "Process started with " << argc << " arguments" << std::endl;
    debug_log.flush();
    
    try {
        debug_log << "Current working directory: " << std::filesystem::current_path() << std::endl;
    } catch (...) {
        debug_log << "ERROR: Could not get current directory" << std::endl;
    }
    debug_log.flush();
    
    for (int i = 0; i < argc; i++) {
        debug_log << "arg[" << i << "] = " << (argv[i] ? argv[i] : "NULL") << std::endl;
    }
    debug_log.flush();
    
    debug_log << "About to enter try block..." << std::endl;
    debug_log.flush();
    
    try
    {
        debug_log << "Inside try block - CryoLSP starting..." << std::endl;

        // Parse command line arguments
        debug_log << "About to parse command line arguments..." << std::endl;
        debug_log.flush();
        
        CommandLineArgs args;
        try {
            args = parse_command_line(argc, argv);
            debug_log << "Command line parsed successfully" << std::endl;
        } catch (const std::exception& e) {
            debug_log << "FATAL: Exception parsing command line: " << e.what() << std::endl;
            debug_log.flush();
            return 1;
        } catch (...) {
            debug_log << "FATAL: Unknown exception parsing command line" << std::endl;
            debug_log.flush();
            return 1;
        }

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

        debug_log << "About to set up logging..." << std::endl;
        debug_log.flush();
        
        // Setup logging with error handling (now that OS utility is initialized)
        try {
            setup_logging(debug_log);
            debug_log << "Logging setup completed successfully" << std::endl;
            debug_log.flush();
        } catch (const std::exception& e) {
            debug_log << "FATAL: Logging setup failed: " << e.what() << std::endl;
            debug_log.flush();
            return 1;
        } catch (...) {
            debug_log << "FATAL: Unknown exception during logging setup" << std::endl;
            debug_log.flush();
            return 1;
        }

        // Setup signal handling for graceful shutdown
        handle_shutdown_signals();

        std::cout << "Creating server configuration..." << std::endl;
        // Create server configuration
        LSPServer::Config config = create_server_config(args);

        // Output the exact message the VS Code extension is looking for
        std::cout << "Starting CryoLSP server on " << args.host << ":" << args.port << std::endl;
        std::cout.flush(); // Ensure immediate output

        // Log startup information (using debug_log since logger is broken)
        debug_log << "Starting CryoLSP server on " << args.host << ":" << args.port << std::endl;
#ifdef _WIN32
        debug_log << "Process ID: " << GetCurrentProcessId() << std::endl;
#else
        debug_log << "Process ID: " << getpid() << std::endl;
#endif
        debug_log.flush();

        debug_log << "About to create LSP server instance..." << std::endl;
        debug_log.flush();
        
        // Declare server variable before try block for proper scoping
        LSPServer* server = nullptr;
        
        try {
            // Skip logger since it's broken
            debug_log << "Creating LSP server instance..." << std::endl;
            debug_log.flush();
            
            debug_log << "Creating LSPServer object..." << std::endl;
            debug_log.flush();
            
            server = new LSPServer(config);
            g_server_instance = server;
            
            debug_log << "LSPServer object created successfully" << std::endl;
            debug_log.flush();
        } catch (const std::exception& e) {
            debug_log << "FATAL: Exception creating LSP server: " << e.what() << std::endl;
            debug_log.flush();
            return 1;
        } catch (...) {
            debug_log << "FATAL: Unknown exception creating LSP server" << std::endl;
            debug_log.flush();
            return 1;
        }
        
        if (!server) {
            std::cerr << "FATAL: Server pointer is null after creation" << std::endl;
            std::cout << "FATAL: Server pointer is null after creation" << std::endl;
            return 1;
        }

        debug_log << "About to initialize LSP server..." << std::endl;
        debug_log.flush();
        
        bool initialized = false;
        try {
            // Move server to heap to avoid potential stack issues
            debug_log << "Calling server.initialize()..." << std::endl;
            debug_log.flush();
            
            initialized = server->initialize();
            
            debug_log << "server.initialize() returned: " << (initialized ? "true" : "false") << std::endl;
            debug_log.flush();
            
        } catch (const std::exception& e) {
            debug_log << "FATAL: Exception during LSP server initialization: " << e.what() << std::endl;
            debug_log.flush();
            delete server;
            return 1;
        } catch (...) {
            debug_log << "FATAL: Unknown exception during LSP server initialization" << std::endl;
            debug_log.flush();
            delete server;
            return 1;
        }
        
        if (!initialized)
        {
            debug_log << "FATAL: Failed to initialize LSP server" << std::endl;
            debug_log << "This is a SERVER-SIDE problem. Check:" << std::endl;
            debug_log << "  1. Compiler initialization" << std::endl;
            debug_log << "  2. LSP provider setup" << std::endl;
            debug_log << "  3. Required libraries available" << std::endl;
            debug_log.flush();
            delete server;
            return 1;
        }
        
        debug_log << "LSP server initialized successfully" << std::endl;
        debug_log.flush();

        debug_log << "Starting LSP server..." << std::endl;
        debug_log << "Server configuration:" << std::endl;
        debug_log << "  Host: " << args.host << std::endl;
        debug_log << "  Port: " << args.port << std::endl;
        debug_log << "  Log level: debug" << std::endl;
        debug_log.flush();
        
        debug_log << "Starting LSP server..." << std::endl;
        debug_log.flush();
        if (!server->start())
        {
            debug_log << "FATAL: Failed to start LSP server" << std::endl;
            debug_log << "This is a SERVER-SIDE problem. Check:" << std::endl;
            debug_log << "  1. Port " << args.port << " is available" << std::endl;
            debug_log << "  2. Permission to bind to port" << std::endl;
            debug_log << "  3. Firewall settings" << std::endl;
            debug_log.flush();
            
            debug_log << "ERROR: Failed to start LSP server" << std::endl;
            debug_log << "Server configuration - Host: " << config.host << ", Port: " << config.port << std::endl;
            debug_log.flush();
            return 1;
        }

        std::cout << "LSP server started successfully on port " << args.port << std::endl;
        debug_log << "LSP server started successfully" << std::endl;
        debug_log << "Ready to accept client connections..." << std::endl;
        debug_log.flush();

        // Keep server running (this will block until shutdown signal)
        // Wait for shutdown signal - in a real implementation, this would wait for SIGTERM/SIGINT
        while (server->is_running())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        debug_log << "LSP server shutting down..." << std::endl;
        debug_log.flush();

        // Cleanup
        server->stop();
        delete server;

        debug_log << "LSP server stopped successfully" << std::endl;
        debug_log.flush();
        
        debug_log << "LSP server exiting normally" << std::endl;
        debug_log.flush();
        debug_log.close();

        return 0;
    }
    catch (const std::exception &e)
    {
        // Log to debug file since normal logger may not be available
        std::ofstream error_log("/workspaces/CryoLang/logs/cryo-lsp-fatal-error.log", std::ios::app);
        error_log << "Fatal error: " << e.what() << std::endl;
        error_log.flush();
        // Logger not available - already logged to error file above
        return 1;
    }
    catch (...)
    {
        // Log to debug file since normal logger may not be available
        std::ofstream error_log("/workspaces/CryoLang/logs/cryo-lsp-fatal-error.log", std::ios::app);
        error_log << "Unknown fatal error occurred" << std::endl;
        error_log.flush();
        // Logger not available - already logged to error file above
        return 1;
    }
}
