#include "LanguageServer.hpp"
#include "Transport.hpp"
#include "TcpTransport.hpp"
#include "Logger.hpp"
#include <iostream>
#include <memory>
#include <filesystem>
#include <cstdlib>

using namespace Cryo::LSP;

std::string find_log_directory()
{
    // Try to find the CryoLang project root and create a logs directory
    std::vector<std::string> possible_roots = {
        // Current working directory + logs
        (std::filesystem::current_path() / "logs").string(),
        // Parent directories (look for CryoLang project structure)
        (std::filesystem::current_path().parent_path() / "logs").string(),
        (std::filesystem::current_path().parent_path().parent_path() / "logs").string(),
        (std::filesystem::current_path().parent_path().parent_path().parent_path() / "logs").string()};

    // Add some common workspace locations
#ifdef _WIN32
    possible_roots.push_back("C:\\Programming\\apps\\CryoLang\\logs");
#else
    // Common Linux dev container paths
    possible_roots.push_back("/workspaces/CryoLang/logs");

    // Check if HOME is set and add some common paths
    const char *home = std::getenv("HOME");
    if (home)
    {
        possible_roots.push_back(std::string(home) + "/CryoLang/logs");
        possible_roots.push_back(std::string(home) + "/workspace/CryoLang/logs");
    }
#endif

    // Try each path and use the first one where we can create/access the logs directory
    for (const auto &log_path : possible_roots)
    {
        try
        {
            std::filesystem::create_directories(log_path);
            if (std::filesystem::exists(log_path) && std::filesystem::is_directory(log_path))
            {
                return log_path;
            }
        }
        catch (...)
        {
            // Continue to next option
        }
    }

    // Fallback to current directory
    return std::filesystem::current_path().string() + "/logs";
}

int main(int argc, char *argv[])
{
    try
    {
        // Initialize logger with cross-platform log file path
        Logger &logger = Logger::instance();
        logger.enable_debug_mode(true); // Enable debug logging

        // Find appropriate log directory and create log file path
        std::string log_dir = find_log_directory();
        std::string log_file = (std::filesystem::path(log_dir) / "cryo-lsp.log").string();

        logger.set_file_output(log_file); // Write to log file
        logger.set_console_output(false); // Disable console output (interferes with LSP stdio)
        logger.info("LSP", "==== CryoLSP Language Server Starting ====");
        logger.info("LSP", "Version: Debug Build");
        logger.info("LSP", "Log file: " + log_file);
        logger.info("LSP", "Command line args: " + std::to_string(argc));
        for (int i = 0; i < argc; i++) {
            logger.info("LSP", "  arg[" + std::to_string(i) + "]: " + std::string(argv[i]));
        }
        logger.info("LSP", "Working directory: " + std::filesystem::current_path().string());
        logger.info("LSP", "Waiting for VS Code initialize request...");

        // Debug: Create a file to prove the server process actually started
        try {
            std::ofstream debug_file("c:\\Programming\\apps\\CryoLang\\logs\\server_started.txt");
            debug_file << "CryoLSP server started at " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
            debug_file.close();
            logger.info("LSP", "Debug file created: server_started.txt");
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