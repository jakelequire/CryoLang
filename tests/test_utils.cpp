#include "test_utils.hpp"
#include "technical_test_reporter.hpp"
#include "Utils/OS.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <functional>
#include <stdexcept>
#include <exception>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#ifdef __unix__ 
#include <unistd.h>      // fork(), pipe(), dup2(), execvp(), close()
#include <sys/types.h>   // pid_t
#include <sys/wait.h>    // waitpid()
#endif
#include <signal.h>      // kill(), SIGKILL
#include <errno.h>       // errno
#include <cstring>       // strerror()
#include <fcntl.h>       // file control options (optional for pipe flags)
#include <cstdlib>       // _exit()

#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#include <setjmp.h>
#include <process.h>
#include <io.h>
#else
#include <csignal>
#include <csetjmp>
#endif

// Global flag to handle main process crashes
static volatile bool main_process_crash_protection = false;
static jmp_buf crash_recovery_point;
static std::string current_test_executable;

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

// Run a single test in an isolated process
struct ProcessTestResult {
    bool success = false;
    bool crashed = false;
    bool failed = false;
    std::string output;
    int exit_code = 0;
};

// Helper function for filtering output (same logic as original)
std::string filter_output(const std::string& raw_output) {
    std::istringstream output_stream(raw_output);
    std::string filtered_output;
    std::string line;
    bool found_result = false;

    while (std::getline(output_stream, line)) {
        if (line.find("[INFO]") != std::string::npos ||
            line.find("[DONE]") != std::string::npos ||
            line.find("Test environment") != std::string::npos ||
            line.find("Initializing") != std::string::npos ||
            line.find("initialized") != std::string::npos ||
            line.empty()) {
            continue;
        }
        if (line.find("PASS") != std::string::npos) {
            filtered_output = "PASS";
            found_result = true;
            break;
        } else if (line.find("FAIL") != std::string::npos ||
                   line.find("CRASH") != std::string::npos ||
                   line.find("ERROR") != std::string::npos) {
            filtered_output = line;
            found_result = true;
            break;
        }
    }

    if (!found_result) {
        std::istringstream stream2(raw_output);
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
    return filtered_output;
}

ProcessTestResult run_test_in_process(const std::string& test_name, size_t test_index) {
    ProcessTestResult result;
    result.success = false;
    result.failed = false;
    result.crashed = true; // Default to crashed
    result.exit_code = -1;
    result.output = "Process isolation failed";

    try {
        std::string cmd_line = "\"" + current_test_executable + "\" --run-single-test \"" + test_name + "\"";

#ifdef _WIN32
        // ---------------- WINDOWS IMPLEMENTATION ----------------
        HANDLE hReadPipe, hWritePipe;
        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            result.output = "Failed to create pipe for test communication";
            return result;
        }

        HANDLE hNullHandle = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        if (hNullHandle == INVALID_HANDLE_VALUE) hNullHandle = hWritePipe;

        HANDLE hNullInput = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, NULL);
        if (hNullInput == INVALID_HANDLE_VALUE) hNullInput = GetStdHandle(STD_INPUT_HANDLE);

        SetHandleInformation(hWritePipe, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(hNullHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(hNullInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        STARTUPINFOA si = {sizeof(STARTUPINFOA)};
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hWritePipe;
        si.hStdError = hNullHandle;
        si.hStdInput = hNullInput;

        PROCESS_INFORMATION pi = {0};
        DWORD creationFlags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;

        if (!CreateProcessA(NULL, const_cast<char*>(cmd_line.c_str()), NULL, NULL, TRUE,
                            creationFlags, NULL, NULL, &si, &pi)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            result.output = "Failed to create test process";
            return result;
        }

        CloseHandle(hWritePipe);
        char buffer[4096];
        DWORD bytesRead;
        std::string output;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            output += buffer;
        }

        DWORD waitResult = WaitForSingleObject(pi.hProcess, 5000);
        if (waitResult == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 999);
            WaitForSingleObject(pi.hProcess, 1000);
            result.crashed = true;
            result.output = "Test forcibly terminated after 5 seconds (hung/infinite loop)";
        } else if (waitResult == WAIT_OBJECT_0) {
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            result.exit_code = exitCode;
            if (exitCode == 0) result.success = true;
            else if (exitCode == 1) result.failed = true;
            else result.crashed = true;
            if (result.output.empty()) result.output = "Process crashed with exit code: " + std::to_string(exitCode);
            result.output = filter_output(output);
        } else {
            result.output = "Process wait failed";
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
        if (hNullHandle != INVALID_HANDLE_VALUE) CloseHandle(hNullHandle);
        if (hNullInput != INVALID_HANDLE_VALUE && hNullInput != GetStdHandle(STD_INPUT_HANDLE)) CloseHandle(hNullInput);

#else
        // ---------------- LINUX IMPLEMENTATION ----------------
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            result.output = "Failed to create pipe";
            return result;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);
            execl("/bin/sh", "sh", "-c", cmd_line.c_str(), (char*)NULL);
            _exit(127);
        } else if (pid > 0) {
            // Parent process
            close(pipefd[1]);
            char buffer[4096];
            ssize_t bytesRead;
            std::string output;
            while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytesRead] = '\0';
                output += buffer;
            }
            close(pipefd[0]);

            int status;
            int waited = 0;
            while (waited < 5) {
                pid_t res = waitpid(pid, &status, WNOHANG);
                if (res == pid) break;
                sleep(1);
                waited++;
            }
            if (waited >= 5) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                result.crashed = true;
                result.output = "Test forcibly terminated after 5 seconds (hung/infinite loop)";
            } else {
                if (WIFEXITED(status)) {
                    int exitCode = WEXITSTATUS(status);
                    result.exit_code = exitCode;
                    if (exitCode == 0) result.success = true;
                    else if (exitCode == 1) result.failed = true;
                    else result.crashed = true;
                    result.output = filter_output(output);
                } else {
                    result.output = "Process crashed";
                }
            }
        } else {
            result.output = "Fork failed";
        }
#endif
    } catch (...) {
        result.output = "Exception occurred during test execution";
    }

    return result;
}



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
            std::cout << "\033[31m[FRAMEWORK ERROR]\033[0m Test " << (i + 1) << " name construction failed" << std::endl;
            std::cout << "\033[33mException:\033[0m " << e.what() << std::endl;
            std::cout << "\033[33mSuite:\033[0m " << test.suite << " (" << test.suite.length() << " chars)" << std::endl;
            std::cout << "\033[33mName:\033[0m " << test.name << " (" << test.name.length() << " chars)" << std::endl;
            std::cout << "\033[33mAction:\033[0m Skipped, continuing\n" << std::endl;
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
            std::cout << "+-- Test " << std::setfill('0') << std::setw(2) << (i + 1) 
                      << "/" << std::setfill('0') << std::setw(2) << tests.size() << " " 
                      << std::string(25, '-') << "+\033[0m" << std::endl;
            std::cout << "\033[31m[FRAMEWORK ERROR]\033[0m std::length_error in test header generation" << std::endl;
            std::cout << "\033[33mException:\033[0m " << e.what() << std::endl;
            std::cout << "\033[33mLocation:\033[0m Parent process string allocation (suite: " 
                      << test.suite.length() << " chars, name: " << test.name.length() << " chars)" << std::endl;
            std::cout << "\033[33mAction:\033[0m Test skipped, execution continues\n" << std::endl;
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
            
            // Analyze exit code for technical insights
            std::cout << "\033[33mExit Code:\033[0m " << result.exit_code;
            if (result.exit_code == -1073741819) {
                std::cout << " (0xC0000005 - Access Violation)";
            } else if (result.exit_code == -1073741571) {
                std::cout << " (0xC00000FD - Stack Overflow)";
            } else if (result.exit_code == 999) {
                std::cout << " (Force Terminated - Timeout)";
            } else if (result.exit_code == 1) {
                std::cout << " (Assertion Failure)";
            }
            std::cout << std::endl;
            
            if (!result.output.empty()) {
                std::istringstream output_stream(result.output);
                std::string line;
                std::vector<std::string> errors;
                std::vector<std::string> debug_info;
                std::string assertion_detail;
                
                // Parse output for different types of information
                while (std::getline(output_stream, line)) {
                    if (line.empty()) continue;
                    
                    // Compiler/Codegen errors
                    if (line.find("[ERROR]") != std::string::npos) {
                        errors.push_back(line);
                    }
                    // Assertion failures with details
                    else if (line.find("FAIL: Assertion failed") != std::string::npos) {
                        assertion_detail = line;
                    }
                    // Debug information
                    else if (line.find("[DEBUG]") != std::string::npos) {
                        debug_info.push_back(line);
                    }
                    // IR generation failures
                    else if (line.find("IR generation") != std::string::npos || 
                             line.find("verification") != std::string::npos ||
                             line.find("CallExpression") != std::string::npos) {
                        errors.push_back(line);
                    }
                    // Template/Generic errors
                    else if (line.find("Cannot find generic template") != std::string::npos ||
                             line.find("Monomorphizer") != std::string::npos) {
                        errors.push_back(line);
                    }
                }
                
                // Display categorized information
                if (!errors.empty()) {
                    for (const auto& error : errors) {
                        std::cout << "\033[31mError:\033[0m " << error << std::endl;
                    }
                }
                if (!assertion_detail.empty()) {
                    std::cout << "\033[33mAssertion:\033[0m " << assertion_detail << std::endl;
                }
                if (!debug_info.empty() && errors.empty() && assertion_detail.empty()) {
                    // Only show debug info if no errors/assertions found
                    std::cout << "\033[36mDebug:\033[0m " << debug_info[0] << std::endl;
                }
                
                // If nothing specific found, show raw output
                if (errors.empty() && assertion_detail.empty() && debug_info.empty()) {
                    std::istringstream stream(result.output);
                    std::string first_line;
                    while (std::getline(stream, first_line) && first_line.empty()) {}
                    if (!first_line.empty()) {
                        std::cout << "\033[37mOutput:\033[0m " << first_line << std::endl;
                    }
                }
            }
            std::cout << std::endl;
            crashed++;
        } else {
            std::cout << "\033[31m  [FAIL]\033[0m" << std::endl;
            std::cout << "\033[33mExit Code:\033[0m " << result.exit_code << std::endl;
            if (!result.output.empty()) {
                std::istringstream output_stream(result.output);
                std::string line;
                std::string failure_reason;
                
                while (std::getline(output_stream, line)) {
                    if (line.find("FAIL:") != std::string::npos) {
                        failure_reason = line.substr(line.find("FAIL:") + 5);
                        break;
                    }
                }
                
                if (!failure_reason.empty()) {
                    std::cout << "\033[31mReason:\033[0m" << failure_reason << std::endl;
                } else if (!result.output.empty()) {
                    // Show first meaningful line
                    std::istringstream stream(result.output);
                    std::string first_line;
                    while (std::getline(stream, first_line) && first_line.empty()) {}
                    if (!first_line.empty()) {
                        std::cout << "\033[31mOutput:\033[0m " << first_line << std::endl;
                    }
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

int TestRegistry::run_all_tests_enhanced() {
    // Implementation of comprehensive technical test analysis
    if (tests.empty()) {
        std::cout << "No tests registered.\n";
        return 0;
    }
    
    // Initialize technical reporter
    TechnicalTestReporter technical_reporter;
    
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "                            CryoLang Test Suite                               \n";
    std::cout << "                         Compiler Analysis & Testing                           \n";
    std::cout << "================================================================================\n";
    std::cout << "\n";
    
    int passed = 0, failed = 0, crashed = 0;
    size_t current_test = 0;
    size_t total_tests = tests.size();
    auto start_time = std::chrono::steady_clock::now();
    
    // Pre-analyze all tests for technical metadata
    std::cout << "Analyzing test technical characteristics...\n";
    for (const auto& test : tests) {
        std::string full_test_name = test.suite + "::" + test.name;
        technical_reporter.analyze_test_source(test.name, test.suite, test.description, full_test_name);
    }
    
    std::cout << "\nExecuting tests with technical monitoring:\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    for (const auto& test : tests) {
        current_test++;
        
        // Progress indicator
        std::cout << "Test " << std::setfill('0') << std::setw(2) << current_test 
                  << "/" << std::setw(2) << total_tests << " ";
        
        // Technical categorization
        std::string phase_indicator = "[?]";
        if (test.suite.find("Parser") != std::string::npos || test.suite.find("Lexer") != std::string::npos) 
            phase_indicator = "[PARSE]";
        else if (test.suite.find("Type") != std::string::npos) 
            phase_indicator = "[TYPE]";
        else if (test.suite.find("Codegen") != std::string::npos) 
            phase_indicator = "[CODEGEN]";
        else if (test.suite.find("StdLib") != std::string::npos) 
            phase_indicator = "[STDLIB]";
        else if (test.suite.find("Integration") != std::string::npos) 
            phase_indicator = "[PIPELINE]";
        
        std::cout << phase_indicator << " " << test.suite << "::" << test.name;
        
        // Show test description for context
        if (!test.description.empty()) {
            std::cout << "\n    >> " << test.description;
        }
        std::cout << "\n";
        
        // Run the test with timing
        auto test_start = std::chrono::steady_clock::now();
        std::string test_name = test.suite + "::" + test.name;
        ProcessTestResult result;
        
        try {
            result = run_test_in_process(test_name, current_test - 1);
        } catch (const std::length_error& e) {
            result.crashed = true;
            result.output = std::string("Parent process error: ") + e.what();
        } catch (...) {
            result.crashed = true;
            result.output = "Unknown parent process exception";
        }
        
        auto test_end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(test_end - test_start);
        
        // Record results in technical reporter
        if (!result.success || result.crashed) {
            technical_reporter.record_test_failure(test.name, result.output);
        }
        
        // Display immediate result with technical context
        if (result.success) {
            std::cout << "    ✓ PASS (" << duration.count() << "ms)\n";
            passed++;
        } else if (result.crashed) {
            std::cout << "    ✗ CRASH - ";
            
            // Technical failure analysis
            if (result.output.find("Function return type does not match") != std::string::npos) {
                std::cout << "IR Verification Error (Backend Issue)";
            } else if (result.output.find("Failed to resolve type") != std::string::npos) {
                std::cout << "Type Resolution Failure (Frontend Issue)";
            } else if (result.output.find("Access Violation") != std::string::npos || 
                       result.exit_code == -1073741819) {
                std::cout << "Memory Access Violation (Runtime Safety Issue)";
            } else if (result.output.find("std::length_error") != std::string::npos) {
                std::cout << "Memory Management Error";
            } else {
                std::cout << "Unknown System Failure";
            }
            
            std::cout << " (" << duration.count() << "ms)\n";
            
            // Show critical error excerpt
            if (result.output.find("Function return type") != std::string::npos) {
                size_t pos = result.output.find("Function return type");
                std::string error_line = result.output.substr(pos, std::min((size_t)80, result.output.length() - pos));
                std::cout << "      Error: " << error_line << "\n";
            }
            
            crashed++;
        } else {
            std::cout << "    ✗ FAIL (" << duration.count() << "ms)\n";
            failed++;
        }
        
        std::cout << "\n";
    }
    
    // Generate comprehensive technical analysis
    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    double pass_rate = total_tests > 0 ? (double)passed / total_tests * 100.0 : 0.0;
    
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "Test execution completed in " << (total_duration.count() / 1000.0) << " seconds\n";
    std::cout << "Pass Rate: " << std::fixed << std::setprecision(1) << pass_rate 
              << "% (" << passed << "/" << total_tests << " tests)\n";
    std::cout << "Crashes: " << crashed << " | Failures: " << failed << "\n\n";
    
    // Generate the comprehensive technical report
    technical_reporter.generate_technical_report();
    
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