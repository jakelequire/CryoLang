#include "LSPServer.hpp"
#include <iostream>
#include <exception>
#include <signal.h>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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
        // Check for socket mode
        bool use_socket = false;
        int socket_port = 7777;
        
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--socket" && i + 1 < argc) {
                // Handle --socket <port> format
                use_socket = true;
                socket_port = std::atoi(argv[i + 1]);
                std::cerr << "[LSP] Using TCP socket mode on port " << socket_port << std::endl;
                break;
            } else if (arg.find("--socket=") == 0) {
                // Handle --socket=<port> format (VS Code automatically adds this)
                use_socket = true;
                socket_port = std::atoi(arg.substr(9).c_str());
                std::cerr << "[LSP] Using TCP socket mode on port " << socket_port << std::endl;
                break;
            }
        }
        
        if (!use_socket) {
            std::cerr << "[LSP] Using stdio mode (default)" << std::endl;
        }
        
        // Log process info for debugging
        std::cerr << "[LSP] CryoLSP server starting..." << std::endl;
        #ifdef _WIN32
        std::cerr << "[LSP] Process ID: " << GetCurrentProcessId() << std::endl;
        #else
        std::cerr << "[LSP] Process ID: " << getpid() << std::endl;
        #endif
        std::cerr << "[LSP] Command line args: ";
        for (int i = 0; i < argc; i++) {
            std::cerr << argv[i] << " ";
        }
        std::cerr << std::endl;
        
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
        if (use_socket) {
            g_server = std::make_unique<Cryo::LSP::LSPServer>(socket_port);
        } else {
            g_server = std::make_unique<Cryo::LSP::LSPServer>();
        }
        
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
