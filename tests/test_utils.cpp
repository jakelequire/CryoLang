#include "test_utils.hpp"
#include "Utils/OS.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#include <setjmp.h>
#endif


namespace CryoTest {

#ifdef _WIN32
// Signal-based crash protection for Windows
static jmp_buf crash_jump_buffer;
static bool crash_detected = false;

void crash_signal_handler(int signal) {
    crash_detected = true;
    
    std::cout << "\n\033[33m  [SYSTEM CRASH DETECTED]\033[0m" << std::endl;
    std::cout << "    \033[90m+-- \033[33mSignal:\033[0m " << signal;
    
    switch (signal) {
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
    
    longjmp(crash_jump_buffer, 1);
}

// Safe test execution using signal handling
int execute_test_with_seh_protection(std::function<void()> test_function) {
    // Set up signal handlers
    auto old_sigsegv = signal(SIGSEGV, crash_signal_handler);
    auto old_sigabrt = signal(SIGABRT, crash_signal_handler);
    auto old_sigfpe = signal(SIGFPE, crash_signal_handler);
    auto old_sigill = signal(SIGILL, crash_signal_handler);
    
    crash_detected = false;
    
    int result = 0;
    if (setjmp(crash_jump_buffer) == 0) {
        // Normal execution
        test_function();
    } else {
        // We jumped here from signal handler
        result = 1;
    }
    
    // Restore original signal handlers
    signal(SIGSEGV, old_sigsegv);
    signal(SIGABRT, old_sigabrt);
    signal(SIGFPE, old_sigfpe);
    signal(SIGILL, old_sigill);
    
    return result;
}
#endif

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
    std::cout << "\033[1;94m|\033[0m                    \033[1;37mCryoLang Test Suite\033[0m                     \033[1;94m|\033[0m" << std::endl;
    std::cout << "\033[1;94m+==============================================================+\033[0m" << std::endl;
    std::cout << "\nExecuting \033[1;37m" << tests.size() << "\033[0m test cases...\n" << std::endl;
    
    for (size_t i = 0; i < tests.size(); ++i) {
        const auto& test = tests[i];
        
        // Test header with counter
        std::cout << "\033[2m+-- Test " << std::setfill('0') << std::setw(2) << (i + 1) 
                  << "/" << std::setfill('0') << std::setw(2) << tests.size() << " " 
                  << std::string(48 - test.suite.length() - test.name.length(), '-') << "+\033[0m" << std::endl;
        
        std::cout << "\033[2m|\033[0m \033[1;36m" << test.suite << "::" << test.name << "\033[0m" << std::endl;
        if (!test.description.empty()) {
            std::cout << "\033[2m|\033[0m \033[90m" << test.description << "\033[0m" << std::endl;
        }
        std::cout << "\033[2m+" << std::string(61, '-') << "+\033[0m" << std::endl;
        
        try {
#ifdef _WIN32
            // Use Windows SEH protection to catch access violations
            int crash_result = execute_test_with_seh_protection(test.test_function);
            if (crash_result == 0) {
                std::cout << "\033[32m  [PASS]\033[0m\n" << std::endl;
                passed++;
            } else {
                crashed++;
            }
#else
            // Unix-like systems - regular execution
            test.test_function();
            std::cout << "\033[32m  [PASS]\033[0m\n" << std::endl;
            passed++;
#endif
        } catch (const AssertionError& e) {
            // Handle assertion failures with enhanced context
            std::cout << "\033[31m  [FAIL]\033[0m" << std::endl;
            
            if (!e.file_name.empty()) {
                std::cout << "    \033[90m+-- \033[31mLocation:\033[0m " << e.file_name << ":" << e.line_number << std::endl;
                std::cout << "    \033[90m+-- \033[31mCondition:\033[0m " << e.condition << std::endl;
                
                if (!e.expected_value.empty() && !e.actual_value.empty()) {
                    std::cout << "    \033[90m+-- \033[32mExpected:\033[0m " << e.expected_value << std::endl;
                    std::cout << "    \033[90m+-- \033[31mActual:\033[0m   " << e.actual_value << std::endl;
                }
                
                // Show compilation stage if available
                if (!e.compilation_stage.empty()) {
                    std::cout << "    \033[90m+-- \033[33mStage:\033[0m " << e.compilation_stage << std::endl;
                }
                
                // Show Cryo diagnostics if available
                if (!e.cryo_diagnostics.empty() && e.cryo_diagnostics != "No errors or warnings") {
                    std::cout << "    \033[90m+-- \033[35mCryo Diagnostics:\033[0m" << std::endl;
                    std::istringstream diag_stream(e.cryo_diagnostics);
                    std::string line;
                    while (std::getline(diag_stream, line)) {
                        if (!line.empty()) {
                            std::cout << "    \033[90m    " << line << "\033[0m" << std::endl;
                        }
                    }
                }
                
                // Show Cryo source context if available
                if (!e.cryo_source.empty() && e.cryo_source != "Source context unavailable") {
                    std::cout << "    \033[90m+-- \033[36mCryo Source Context:\033[0m" << std::endl;
                    std::istringstream source_stream(e.cryo_source);
                    std::string line;
                    while (std::getline(source_stream, line)) {
                        if (!line.empty()) {
                            std::cout << "    \033[90m    " << line << "\033[0m" << std::endl;
                        }
                    }
                }
                
                // Show C++ source context for better debugging
                CryoTest::CryoTestBase helper;
                std::string context = helper.get_source_context(e.file_name, e.line_number);
                if (!context.empty() && context != "Source file not available" && context != "Invalid line number") {
                    std::cout << "    \033[90m+-- \033[37mC++ Source Context:\033[0m" << std::endl;
                    std::istringstream context_stream(context);
                    std::string line;
                    while (std::getline(context_stream, line)) {
                        std::cout << "    \033[90m    " << line << "\033[0m" << std::endl;
                    }
                }
            } else {
                std::cout << "    \033[90m+-- \033[31mError:\033[0m " << e.what() << std::endl;
            }
            std::cout << std::endl;
            failed++;
        } catch (const std::bad_alloc& e) {
            std::cout << "\033[33m  [CRASH]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m Out of memory" << std::endl;
            std::cout << "    \033[90m+-- \033[33mDetails:\033[0m " << e.what() << std::endl;
            std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite continues\n" << std::endl;
            crashed++;
        } catch (const std::exception& e) {
            std::cout << "\033[31m  [FAIL]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[31mException:\033[0m " << e.what() << "\n" << std::endl;
            failed++;
        } catch (...) {
            // Catch any other exceptions that might occur
            std::cout << "\033[33m  [CRASH]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[33mCrash Type:\033[0m Unknown exception" << std::endl;
            std::cout << "    \033[90m+-- \033[33mNote:\033[0m Test crashed but suite continues\n" << std::endl;
            crashed++;
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
        return 0;
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
        return 1;
    }
}

} // namespace CryoTest