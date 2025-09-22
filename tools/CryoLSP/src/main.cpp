#include "LSPServer.hpp"
#include <iostream>
#include <exception>
#include <signal.h>
#include <memory>

// Global server instance for signal handling
std::unique_ptr<Cryo::LSP::LSPServer> g_server = nullptr;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    std::cerr << "[LSP] Received signal " << signal << ", shutting down gracefully..." << std::endl;
    if (g_server) {
        // The server will handle shutdown in its main loop
        std::exit(0);
    }
}

/**
 * @file main.cpp
 * @brief Entry point for the CryoLSP Language Server
 * 
 * This executable implements the Language Server Protocol (LSP) for the Cryo programming language.
 * It communicates with code editors (like VS Code) via JSON-RPC messages over stdin/stdout.
 * 
 * Usage:
 *   cryo-lsp.exe
 * 
 * The server will:
 * 1. Read LSP messages from stdin
 * 2. Process requests using the existing Cryo compiler frontend
 * 3. Send responses and notifications to stdout
 * 4. Provide language features like diagnostics, completions, hover info, etc.
 */

int main(int argc, char* argv[])
{
    try {
        // Critical Windows LSP stdio setup - prevent buffering deadlocks
        std::ios_base::sync_with_stdio(false);
        std::cin.tie(nullptr);
        std::cout.tie(nullptr);
        std::cerr.tie(nullptr);
        
        // Force unbuffered I/O (essential for LSP handshake on Windows)
        std::setvbuf(stdin, nullptr, _IONBF, 0);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        
        // Additional Windows-specific buffering fixes
        std::cin.rdbuf()->pubsetbuf(nullptr, 0);
        std::cout.rdbuf()->pubsetbuf(nullptr, 0);
        std::cerr.rdbuf()->pubsetbuf(nullptr, 0);
        
        // Set up signal handlers for graceful shutdown
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
#ifdef _WIN32
        signal(SIGBREAK, signal_handler);
#endif

        // Create and run the LSP server
        g_server = std::make_unique<Cryo::LSP::LSPServer>();
        
        std::cerr << "[LSP] CryoLSP server starting..." << std::endl;
        
        // The server will run until it receives a shutdown request
        g_server->run();
        
        std::cerr << "[LSP] CryoLSP server shutting down normally" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        // Log any unhandled exceptions to stderr
        // (LSP communication happens over stdout, so stderr is safe for logging)
        std::cerr << "CryoLSP Fatal Error: " << e.what() << std::endl;
        return 1;
        
    } catch (...) {
        std::cerr << "CryoLSP Fatal Error: Unknown exception occurred" << std::endl;
        return 1;
    }
}
