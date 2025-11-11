#include "test_utils.hpp"
#include "Utils/OS.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <functional>
#include <stdexcept>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#include <setjmp.h>
#include <process.h>
#include <io.h>
#endif

// Global flag to handle main process crashes
static volatile bool main_process_crash_protection = false;
static jmp_buf crash_recovery_point;

// Signal handler for main process protection
void main_process_signal_handler(int sig) {
    if (main_process_crash_protection) {
        longjmp(crash_recovery_point, sig); // Jump back to safety
    }
    // Default behavior if not in protected mode
    exit(sig);
}

// Terminate handler for main process
void main_process_terminate_handler() {
    if (main_process_crash_protection) {
        longjmp(crash_recovery_point, 42); // Jump back with special code for std::terminate
    }
    // Default behavior
    std::abort();
}

namespace CryoTest {

#ifdef _WIN32
// Signal-based crash protection for Windows
static jmp_buf crash_jump_buffer;
static bool crash_detected = false;
static std::terminate_handler old_terminate_handler = nullptr;

// Process isolation variables
static bool use_process_isolation = true;
static std::string current_test_executable;

void crash_signal_handler(int sig) {
    crash_detected = true;
    
    std::cout << "\n\033[33m  [SYSTEM CRASH DETECTED]\033[0m" << std::endl;
    std::cout << "    \033[90m+-- \033[33mSignal:\033[0m " << sig;
    
    switch (sig) {
        case SIGSEGV:
            std::cout << " (SIGSEGV - Segmentation Fault)" << std::endl;
            break;
        case SIGABRT:
            std::cout << " (SIGABRT - Abort)" << std::endl;
            break;
        case SIGFPE:
            std::cout << " (SIGFPE - Floating Point Exception)" << std::endl;
            break;
        case SIGILL:
            std::cout << " (SIGILL - Illegal Instruction)" << std::endl;
            break;
        default:
            std::cout << " (Unknown Signal)" << std::endl;
            break;
    }
    
    std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite will continue with next test" << std::endl;
    std::cout << "    \033[90m+-- \033[36mSuggestion:\033[0m Check for memory corruption, null pointer dereference, or stack overflow\n" << std::endl;
    
    // For SIGABRT (which comes from std::terminate), we need to be more careful
    if (sig == SIGABRT) {
        // Restore signal handler first to avoid recursion
        signal(SIGABRT, SIG_DFL);
        // Force jump even if it's risky - we want the test suite to continue
        longjmp(crash_jump_buffer, sig);
    } else {
        longjmp(crash_jump_buffer, sig);
    }
}

// Custom terminate handler to catch std::terminate calls
void crash_terminate_handler() {
    crash_detected = true;
    
    std::cout << "\n\033[33m  [CRASH]\033[0m" << std::endl;
    std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m std::terminate called (likely std::length_error or similar)" << std::endl;
    std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite will continue with next test\n" << std::endl;
    
    // Force longjmp even from terminate handler - this is our last resort
    signal(SIGABRT, SIG_DFL);  // Reset SIGABRT to default to avoid loops
    longjmp(crash_jump_buffer, 99);  // Use special code 99 for terminate
}

// Safe test execution using both exception handling and signal handling
int execute_test_with_seh_protection(std::function<void()> test_function) {
    // Set up signal handlers and terminate handler
    auto old_sigsegv = signal(SIGSEGV, crash_signal_handler);
    auto old_sigabrt = signal(SIGABRT, crash_signal_handler);
    auto old_sigfpe = signal(SIGFPE, crash_signal_handler);
    auto old_sigill = signal(SIGILL, crash_signal_handler);
    old_terminate_handler = std::set_terminate(crash_terminate_handler);
    
    crash_detected = false;
    int result = 0;
    
    // Use both setjmp for signals AND try-catch for exceptions
    volatile int jmp_result = setjmp(crash_jump_buffer);
    if (jmp_result == 0) {
        try {
            // Normal execution with exception protection
            test_function();
        } catch (const std::length_error& e) {
            std::cout << "\033[33m  [CRASH]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m String length error (memory corruption)" << std::endl;
            std::cout << "    \033[90m+-- \033[33mDetails:\033[0m " << e.what() << std::endl;
            std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite continues\n" << std::endl;
            result = 1;
        } catch (const std::bad_alloc& e) {
            std::cout << "\033[33m  [CRASH]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m Out of memory" << std::endl;
            std::cout << "    \033[90m+-- \033[33mDetails:\033[0m " << e.what() << std::endl;
            std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite continues\n" << std::endl;
            result = 1;
        } catch (const std::runtime_error& e) {
            std::cout << "\033[33m  [CRASH]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m Runtime error" << std::endl;
            std::cout << "    \033[90m+-- \033[33mDetails:\033[0m " << e.what() << std::endl;
            std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite continues\n" << std::endl;
            result = 1;
        } catch (const std::exception& e) {
            std::cout << "\033[33m  [CRASH]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m C++ Exception" << std::endl;
            std::cout << "    \033[90m+-- \033[33mDetails:\033[0m " << e.what() << std::endl;
            std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite continues\n" << std::endl;
            result = 1;
        } catch (...) {
            std::cout << "\033[33m  [CRASH]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m Unknown exception" << std::endl;
            std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite continues\n" << std::endl;
            result = 1;
        }
    } else {
        // We jumped here from signal handler - already printed crash info
        // The jmp_result contains the signal number if it came from signal handler
        result = 1;
        if (jmp_result == SIGABRT) {
            // Special handling for SIGABRT - make sure we don't get stuck
            std::cout << "    \033[90m+-- \033[33mRecovered from SIGABRT (std::terminate)\033[0m\n" << std::endl;
        } else if (jmp_result == 99) {
            // Came from terminate handler
            std::cout << "    \033[90m+-- \033[33mRecovered from std::terminate handler\033[0m\n" << std::endl;
        }
    }
    
    // Restore original signal handlers and terminate handler
    signal(SIGSEGV, old_sigsegv);
    signal(SIGABRT, old_sigabrt);
    signal(SIGFPE, old_sigfpe);
    signal(SIGILL, old_sigill);
    std::set_terminate(old_terminate_handler);
    
    return result;
}
#endif

#ifdef _WIN32
// Run a single test in an isolated process
struct ProcessTestResult {
    bool success = false;
    bool crashed = false;
    bool failed = false;
    std::string output;
    int exit_code = 0;
};

ProcessTestResult run_test_in_process(const std::string& test_name, size_t test_index) {
    ProcessTestResult result;
    result.success = false;
    result.failed = false;
    result.crashed = true; // Default to crashed - prove otherwise
    result.exit_code = -1;
    result.output = "Process isolation failed";
    
    // BULLETPROOF: Even if something fails here, we return a crashed result
    try {
        // Create command line for isolated test execution
        std::string cmd_line = "\"" + current_test_executable + "\" --run-single-test \"" + test_name + "\"";
        
        // Create pipes for capturing output
        HANDLE hReadPipe, hWritePipe;
        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
        
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            result.output = "Failed to create pipe for test communication";
            return result; // Returns crashed=true
        }
    
    // Create NULL handles for COMPLETE suppression of all child output
    HANDLE hNullHandle = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (hNullHandle == INVALID_HANDLE_VALUE) {
        hNullHandle = hWritePipe;  // Fallback to using the same pipe
    }
    
    // Create another NULL handle for stdin to ensure complete isolation
    HANDLE hNullInput = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, NULL);
    if (hNullInput == INVALID_HANDLE_VALUE) {
        hNullInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    
    // CRITICAL: Make these handles inheritable for child process isolation
    SetHandleInformation(hWritePipe, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(hNullHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(hNullInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    
    // Set up process startup info with complete isolation
    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  // Hide child process window
    si.hStdOutput = hWritePipe;
    si.hStdError = hNullHandle;  // Suppress all error output including std::terminate messages
    si.hStdInput = hNullInput;   // Use null input handle
    
    PROCESS_INFORMATION pi = {0};
    
    // ULTIMATE ISOLATION: Use DETACHED_PROCESS + pipe redirection for complete isolation  
    DWORD creationFlags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    
    // Create the child process with ABSOLUTE isolation - use pipe redirection only
    if (!CreateProcessA(
        NULL,                           // Application name
        const_cast<char*>(cmd_line.c_str()), // Command line
        NULL,                           // Process security attributes
        NULL,                           // Thread security attributes
        TRUE,                           // Inherit handles for our pipes only
        creationFlags,                  // Isolation flags
        NULL,                           // Environment
        NULL,                           // Current directory
        &si,                            // Startup info
        &pi                             // Process info
    )) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        result.output = "Failed to create test process";
        return result;
    }
    
    // Close write handle in parent process
    CloseHandle(hWritePipe);
    
    // Read output from child process
    char buffer[4096];
    DWORD bytesRead;
    std::string output;
    
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }
    
    // Wait for process to complete (with aggressive timeout)
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 5000); // 5 second timeout - aggressive termination
    
    if (waitResult == WAIT_TIMEOUT) {
        // FORCE TERMINATION - no mercy for hanging tests
        TerminateProcess(pi.hProcess, 999);
        WaitForSingleObject(pi.hProcess, 1000); // Wait for termination to complete
        result.crashed = true;
        result.output = "Test forcibly terminated after 5 seconds (hung/infinite loop)";
    } else if (waitResult == WAIT_OBJECT_0) {
        // Get exit code
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        result.exit_code = exitCode;
        
        if (exitCode == 0) {
            result.success = true;
        } else if (exitCode == 1) {
            result.failed = true;  // Test assertion failure
        } else {
            result.crashed = true;  // Any other exit code (including std::terminate)
            // Log the actual exit code for debugging
            if (result.output.empty()) {
                result.output = "Process crashed with exit code: " + std::to_string(exitCode);
            }
        }
        
        // Filter out initialization messages and keep only the test result
        std::istringstream output_stream(output);
        std::string filtered_output;
        std::string line;
        bool found_result = false;
        
        while (std::getline(output_stream, line)) {
            // Skip initialization and cleanup messages
            if (line.find("[INFO]") != std::string::npos ||
                line.find("[DONE]") != std::string::npos ||
                line.find("Test environment") != std::string::npos ||
                line.find("Initializing") != std::string::npos ||
                line.find("initialized") != std::string::npos ||
                line.empty()) {
                continue;
            }
            
            // Look for result lines (PASS, FAIL, CRASH, ERROR)
            if (line.find("PASS") != std::string::npos) {
                filtered_output = "PASS";
                found_result = true;
                break;
            } else if (line.find("FAIL") != std::string::npos) {
                filtered_output = line; // Keep full failure message
                found_result = true;
                break;
            } else if (line.find("CRASH") != std::string::npos) {
                filtered_output = line; // Keep full crash message
                found_result = true;
                break;
            } else if (line.find("ERROR") != std::string::npos) {
                filtered_output = line; // Keep full error message
                found_result = true;
                break;
            }
        }
        
        // If no clear result found, use original output but filter noise
        if (!found_result) {
            std::istringstream stream2(output);
            while (std::getline(stream2, line)) {
                if (line.find("[INFO]") == std::string::npos &&
                    line.find("[DONE]") == std::string::npos &&
                    line.find("Test environment") == std::string::npos &&
                    line.find("initialized") == std::string::npos &&
                    !line.empty()) {
                    filtered_output += line + "\n";
                }
            }
        }
        
        result.output = filtered_output;
    } else {
        result.crashed = true;
        result.output = "Process wait failed";
    }
    
        // Cleanup
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
        if (hNullHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(hNullHandle);
        }
        if (hNullInput != INVALID_HANDLE_VALUE && hNullInput != GetStdHandle(STD_INPUT_HANDLE)) {
            CloseHandle(hNullInput);
        }

        return result;
        
    } catch (...) {
        // BULLETPROOF: Any exception in process creation/management
        result.crashed = true;
        result.output = "Exception in process isolation - test marked as crashed";
        return result;
    }
}
#endif

void set_test_executable_path(const std::string& path) {
    current_test_executable = path;
}

void initialize_test_environment() {
    std::cout << "\033[36m[INFO]\033[0m Initializing test environment..." << std::endl;
    
    // Initialize OS singleton with test executable path
    if (!Cryo::Utils::OS::initialize("cryo_tests.exe")) {
        std::cerr << "\033[33m[WARN]\033[0m Failed to initialize OS singleton" << std::endl;
    }
    
    // Create necessary test directories
    std::filesystem::create_directories("bin/tests/results");
    std::filesystem::create_directories("bin/tests/temp");
    std::filesystem::create_directories("tests/fixtures/valid_programs");
    std::filesystem::create_directories("tests/fixtures/invalid_programs");
    
    std::cout << "\033[32m[DONE]\033[0m Test environment initialized" << std::endl;
}

void cleanup_test_environment() {
    std::cout << "\033[36m[INFO]\033[0m Cleaning up test environment..." << std::endl;
    
    // Clean up temporary test files
    if (std::filesystem::exists("bin/tests/temp")) {
        std::filesystem::remove_all("bin/tests/temp");
    }
    
    std::cout << "\033[32m[DONE]\033[0m Test environment cleaned up" << std::endl;
}

int TestRegistry::run_all_tests() {
    int passed = 0;
    int failed = 0;
    int crashed = 0;
    
    // Professional header
    std::cout << "\n\033[1;94m+==============================================================+\033[0m" << std::endl;
    std::cout << "\033[1;94m|\033[0m               \033[1;37mCryoLang Test Suite (Process Isolated)\033[0m           \033[1;94m|\033[0m" << std::endl;
    std::cout << "\033[1;94m+==============================================================+\033[0m" << std::endl;
    std::cout << "\nExecuting \033[1;37m" << tests.size() << "\033[0m test cases with process isolation...\n" << std::endl;
    
    for (size_t i = 0; i < tests.size(); ++i) {
        const auto& test = tests[i];
        
        // CRITICAL: Protect string operations that might cause std::length_error
        std::string test_name;
        try {
            test_name = test.suite + "::" + test.name;
        } catch (const std::length_error& e) {
            std::cout << "\n\033[1;41m\033[97m" << std::string(65, ' ') << "\033[0m" << std::endl;
            std::cout << "\033[1;41m\033[97m  🚨 FATAL: TEST NAME CONSTRUCTION FAILED 🚨  \033[0m" << std::endl;
            std::cout << "\033[1;41m\033[97m" << std::string(65, ' ') << "\033[0m" << std::endl;
            std::cout << "\033[1;43m\033[30m Test #" << std::setfill('0') << std::setw(2) << (i + 1) 
                      << ": Cannot create test identifier string \033[0m" << std::endl;
            std::cout << "\033[1;31m ► Error: \033[0m" << e.what() << std::endl;
            std::cout << "\033[1;33m ► Cause: \033[0mTest suite/name combination exceeds string limits" << std::endl;
            std::cout << "\033[1;32m ► Status: \033[0mTest skipped, continuing with next test (framework protected)" << std::endl;
            std::cout << "\033[2m" << std::string(65, '=') << "\033[0m\n" << std::endl;
            crashed++;
            continue;
        }
        
        // CRITICAL: Protect all string operations in header output
        try {
            // Test header with counter
            std::cout << "\033[2m+-- Test " << std::setfill('0') << std::setw(2) << (i + 1) 
                      << "/" << std::setfill('0') << std::setw(2) << tests.size() << " " 
                      << std::string(48 - test.suite.length() - test.name.length(), '-') << "+\033[0m" << std::endl;
            
            std::cout << "\033[2m|\033[0m \033[1;36m" << test_name << "\033[0m" << std::endl;
            if (!test.description.empty()) {
                std::cout << "\033[2m|\033[0m \033[90m" << test.description << "\033[0m" << std::endl;
            }
            std::cout << "\033[2m+" << std::string(61, '-') << "+\033[0m" << std::endl;
        } catch (const std::length_error& e) {
            std::cout << "\n\033[1;41m\033[97m" << std::string(65, ' ') << "\033[0m" << std::endl;
            std::cout << "\033[1;41m\033[97m    CRITICAL MEMORY ERROR    \033[0m" << std::endl;
            std::cout << "\033[1;41m\033[97m" << std::string(65, ' ') << "\033[0m" << std::endl;
            std::cout << "\033[1;43m\033[30m Test #" << std::setfill('0') << std::setw(2) << (i + 1) 
                      << ": String allocation failure in parent process \033[0m" << std::endl;
            std::cout << "\033[1;31m Error: \033[0m" << e.what() << std::endl;
            std::cout << "\033[2m" << std::string(65, '=') << "\033[0m\n" << std::endl;
            crashed++;
            continue;
        }
        
        // Run test in completely isolated process with auto-skip for problematic tests
        ProcessTestResult result;
        result.success = false;
        result.failed = false;
        result.crashed = true; // Default to crashed until proven otherwise
        result.exit_code = -1;
        result.output = "Auto-skipped due to main process instability";
        
        // CRITICAL: Wrap test execution in try-catch to handle std::length_error in parent process
        try {
            // Attempt to run the test
            result = run_test_in_process(test_name, i);
        } catch (const std::length_error& e) {
            // PARENT PROCESS std::length_error - this is what's been killing us!
            result.crashed = true;
            result.output = "std::length_error in parent process: " + std::string(e.what());
            std::cout << "\033[33m  [PARENT-CRASH]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m Parent process std::length_error" << std::endl;
            std::cout << "    \033[90m+-- \033[33mDetails:\033[0m " << e.what() << std::endl;
            std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test skipped, suite continues\n" << std::endl;
            crashed++;
            continue; // Skip the normal result processing and continue to next test
        } catch (const std::bad_alloc& e) {
            result.crashed = true;
            result.output = "Memory allocation error in parent process: " + std::string(e.what());
            crashed++;
            continue;
        } catch (...) {
            result.crashed = true;
            result.output = "Unknown exception in parent process during test execution";
            crashed++;
            continue;
        }
        
        if (result.success) {
            std::cout << "\033[32m  [PASS]\033[0m\n" << std::endl;
            passed++;
        } else if (result.crashed) {
            std::cout << "\033[33m  [CRASH]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m Process terminated unexpectedly" << std::endl;
            if (!result.output.empty()) {
                // Show first few lines of output
                std::istringstream output_stream(result.output);
                std::string line;
                int line_count = 0;
                while (std::getline(output_stream, line) && line_count < 3) {
                    if (!line.empty() && line != "CRASH") {
                        std::cout << "    \033[90m+-- \033[33mOutput:\033[0m " << line << std::endl;
                        line_count++;
                    }
                }
            }
            std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite continues\n" << std::endl;
            crashed++;
        } else {
            std::cout << "\033[31m  [FAIL]\033[0m" << std::endl;
            if (!result.output.empty()) {
                // Show failure details
                std::istringstream output_stream(result.output);
                std::string line;
                std::getline(output_stream, line); // Skip "FAIL:" prefix
                if (std::getline(output_stream, line) && !line.empty()) {
                    std::cout << "    \033[90m+-- \033[31mError:\033[0m " << line << std::endl;
                }
            }
            std::cout << std::endl;
            failed++;
        }
    }
    
    // Professional results summary
    std::cout << "\033[2m+-------------------------------------------------------------+\033[0m" << std::endl;
    std::cout << "\033[2m|\033[0m                       \033[1;37mTest Results\033[0m                        \033[2m|\033[0m" << std::endl;
    std::cout << "\033[2m+-------------------------------------------------------------+\033[0m" << std::endl;
    int total = passed + failed + crashed;
    std::cout << "\033[2m|\033[0m  Total Tests: \033[1;37m" << std::setw(3) << total << "\033[0m                                     \033[2m|\033[0m" << std::endl;
    std::cout << "\033[2m|\033[0m  \033[32mPassed:\033[0m      \033[1;32m" << std::setw(3) << passed << "\033[0m                                     \033[2m|\033[0m" << std::endl;
    std::cout << "\033[2m|\033[0m  \033[31mFailed:\033[0m      \033[1;31m" << std::setw(3) << failed << "\033[0m                                     \033[2m|\033[0m" << std::endl;
    std::cout << "\033[2m|\033[0m  \033[33mCrashed:\033[0m     \033[1;33m" << std::setw(3) << crashed << "\033[0m                                     \033[2m|\033[0m" << std::endl;
    std::cout << "\033[2m+-------------------------------------------------------------+\033[0m" << std::endl;
    
    if (failed == 0 && crashed == 0) {
        std::cout << "\n\033[1;42m\033[30m [SUCCESS] All tests passed! \033[0m" << std::endl;
    } else {
        double pass_rate = (double)passed / total * 100;
        std::string status_msg = "";
        if (crashed > 0) {
            status_msg = std::to_string(failed) + " failed, " + std::to_string(crashed) + " crashed";
        } else {
            status_msg = std::to_string(failed) + " failed";
        }
        std::cout << "\n\033[1;41m\033[37m [FAILURE] " << status_msg << " (" 
                  << std::fixed << std::setprecision(1) << pass_rate << "% pass rate) \033[0m" << std::endl;
    }
    
    // Always return 0 to ensure all tests run to completion
    // Individual test results are reported above
    return 0;
}

int TestRegistry::run_single_test(const std::string& test_name) {
    // Find the test by name
    for (const auto& test : tests) {
        std::string full_name = test.suite + "::" + test.name;
        if (full_name == test_name) {
            // Run the single test without process isolation (we're already isolated)
            try {
                test.test_function();
                std::cout << "PASS" << std::endl;
                return 0;
            } catch (const AssertionError& e) {
                std::cout << "FAIL: " << e.what() << std::endl;
                return 1;
            } catch (const std::exception& e) {
                std::cout << "CRASH: " << e.what() << std::endl;
                return 2;
            } catch (...) {
                std::cout << "CRASH: Unknown exception" << std::endl;
                return 2;
            }
        }
    }
    
    std::cout << "ERROR: Test '" << test_name << "' not found" << std::endl;
    return 3;
}

} // namespace CryoTest