#include "test_utils.hpp"
#include "Utils/OS.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>

namespace CryoTest {

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
        std::cout << "\033[2m+" << std::string(61, '-') << "+\033[0m" << std::endl;
        
        try {
            test.test_function();
            std::cout << "\033[32m  [PASS]\033[0m\n" << std::endl;
            passed++;
        } catch (const AssertionError& e) {
            std::cout << "\033[31m  [FAIL]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[31mAssertion Error:\033[0m " << e.what() << "\n" << std::endl;
            failed++;
        } catch (const std::exception& e) {
            std::cout << "\033[31m  [FAIL]\033[0m" << std::endl;
            std::cout << "    \033[90m+-- \033[31mException:\033[0m " << e.what() << "\n" << std::endl;
            failed++;
        }
    }
    
    // Professional results summary
    std::cout << "\033[2m+-------------------------------------------------------------+\033[0m" << std::endl;
    std::cout << "\033[2m|\033[0m                       \033[1;37mTest Results\033[0m                        \033[2m|\033[0m" << std::endl;
    std::cout << "\033[2m+-------------------------------------------------------------+\033[0m" << std::endl;
    std::cout << "\033[2m|\033[0m  Total Tests: \033[1;37m" << std::setw(3) << (passed + failed) << "\033[0m                                     \033[2m|\033[0m" << std::endl;
    std::cout << "\033[2m|\033[0m  \033[32mPassed:\033[0m      \033[1;32m" << std::setw(3) << passed << "\033[0m                                     \033[2m|\033[0m" << std::endl;
    std::cout << "\033[2m|\033[0m  \033[31mFailed:\033[0m      \033[1;31m" << std::setw(3) << failed << "\033[0m                                     \033[2m|\033[0m" << std::endl;
    std::cout << "\033[2m+-------------------------------------------------------------+\033[0m" << std::endl;
    
    if (failed == 0) {
        std::cout << "\n\033[1;42m\033[30m [SUCCESS] All tests passed! \033[0m" << std::endl;
        return 0;
    } else {
        double pass_rate = (double)passed / (passed + failed) * 100;
        std::cout << "\n\033[1;41m\033[37m [FAILURE] " << failed << " test(s) failed (" 
                  << std::fixed << std::setprecision(1) << pass_rate << "% pass rate) \033[0m" << std::endl;
        return 1;
    }
}

} // namespace CryoTest