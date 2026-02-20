#include "LSP/Server.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/ModuleLoader.hpp"
#include "Utils/OS.hpp"
#include <iostream>
#include <string>
#include <filesystem>
#include <csignal>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif

// Signal handler for crashes - log before dying
static void crash_handler(int sig)
{
    const char *sig_name = "UNKNOWN";
    if (sig == SIGSEGV)
        sig_name = "SIGSEGV";
    else if (sig == SIGABRT)
        sig_name = "SIGABRT";
    else if (sig == SIGFPE)
        sig_name = "SIGFPE";

    // Use CryoLSP logging (writes to log file + stderr)
    CryoLSP::Transport::log(std::string("FATAL: Caught signal ") + sig_name + " - server crashing");

    // Re-raise to get default behavior (core dump / exit)
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

#ifdef _WIN32
// Windows structured exception handler for access violations
static LONG WINAPI windows_exception_handler(EXCEPTION_POINTERS *info)
{
    const char *desc = "Unknown exception";
    if (info && info->ExceptionRecord)
    {
        switch (info->ExceptionRecord->ExceptionCode)
        {
        case EXCEPTION_ACCESS_VIOLATION:
            desc = "Access violation (0xC0000005)";
            break;
        case EXCEPTION_STACK_OVERFLOW:
            desc = "Stack overflow";
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            desc = "Integer divide by zero";
            break;
        }
    }
    CryoLSP::Transport::log(std::string("FATAL: Windows exception: ") + desc + " - server crashing");
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char *argv[])
{
    // Set binary mode for stdin/stdout on Windows
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // Initialize the OS utility singleton (needed by ModuleLoader for path operations)
    Cryo::Utils::OS::initialize(argv[0]);

    // Set global executable path so ModuleLoader::find_stdlib_directory() can search
    // relative to the LSP binary (which lives alongside the compiler in bin/)
    Cryo::ModuleLoader::set_global_executable_path(argv[0]);

    // Install crash handlers for diagnostic logging
    std::signal(SIGSEGV, crash_handler);
    std::signal(SIGABRT, crash_handler);
    std::signal(SIGFPE, crash_handler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(windows_exception_handler);
#endif

    // Parse command line args
    bool debug = false;
    std::string log_file_path;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--debug" || arg == "-d")
        {
            debug = true;
        }
        else if ((arg == "--log-file" || arg == "-l") && i + 1 < argc)
        {
            log_file_path = argv[++i];
        }
        else if (arg == "--version" || arg == "-v")
        {
            std::cerr << "CryoLSP v1.0.0" << std::endl;
            return 0;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cerr << "CryoLSP - Language Server for the Cryo programming language" << std::endl;
            std::cerr << std::endl;
            std::cerr << "Usage: cryolsp [options]" << std::endl;
            std::cerr << std::endl;
            std::cerr << "Options:" << std::endl;
            std::cerr << "  --debug, -d              Enable debug logging" << std::endl;
            std::cerr << "  --log-file, -l <path>    Log to file (truncates on start)" << std::endl;
            std::cerr << "  --version, -v            Show version" << std::endl;
            std::cerr << "  --help, -h               Show this help" << std::endl;
            return 0;
        }
    }

    // Default log file location: next to the binary
    if (log_file_path.empty())
    {
        // Put it in the project root (same dir as bin/)
        std::filesystem::path exe_path = std::filesystem::path(argv[0]).parent_path();
        log_file_path = (exe_path / "cryolsp.log").string();
    }

    // Initialize log file (truncates previous)
    CryoLSP::Transport::initLogFile(log_file_path);

    CryoLSP::Transport::log("CryoLSP v1.0.0 starting on stdio");
    CryoLSP::Transport::log("Log file: " + log_file_path);

    if (debug)
    {
        CryoLSP::Transport::log("Debug mode enabled");
    }

    CryoLSP::Server server;
    return server.run();
}
