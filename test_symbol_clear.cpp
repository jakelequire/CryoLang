#include <iostream>
#include "Compiler/CompilerInstance.hpp"
#include "tests/include/test_helpers.hpp"

using namespace Cryo;
using namespace CryoTest;

int main() {
    std::cout << "Testing TypeChecker symbol clearing fix..." << std::endl;
    
    try {
        TypeCheckerTestHelper helper1;
        TypeCheckerTestHelper helper2;
        
        // Test 1: First variable declaration should succeed
        std::cout << "Test 1: First variable declaration..." << std::endl;
        helper1.setup();
        bool success1 = helper1.parse_and_type_check("int unique_test_var_abc123 = 42;");
        std::cout << "Test 1 result: " << (success1 ? "SUCCESS" : "FAILED") << std::endl;
        
        // Test 2: Second identical variable declaration in fresh helper should also succeed
        std::cout << "Test 2: Second identical variable declaration (fresh helper)..." << std::endl;
        helper2.setup();
        bool success2 = helper2.parse_and_type_check("int unique_test_var_abc123 = 42;");
        std::cout << "Test 2 result: " << (success2 ? "SUCCESS" : "FAILED") << std::endl;
        
        if (success1 && success2) {
            std::cout << "\n✓ SYMBOL CLEARING FIX WORKS!" << std::endl;
            std::cout << "Both tests succeeded - symbols are properly isolated between test instances." << std::endl;
            return 0;
        } else {
            std::cout << "\n✗ SYMBOL CLEARING FIX FAILED!" << std::endl;
            std::cout << "One or both tests failed - symbols are still persisting between instances." << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "\n✗ ERROR: " << e.what() << std::endl;
        return 1;
    }
}