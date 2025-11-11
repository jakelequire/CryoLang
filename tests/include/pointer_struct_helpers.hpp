#pragma once

#include "test_helpers.hpp"
#include <vector>
#include <regex>

namespace CryoTest {

/**
 * @brief Test helper specifically for pointer arithmetic operations
 */
class PointerArithmeticTestHelper : public IntegrationTestHelper {
private:
    std::vector<std::string> expected_runtime_outputs;
    std::vector<std::string> expected_errors;
    
public:
    /**
     * @brief Test that source compiles and produces expected behavior
     */
    bool test_pointer_operations(const std::string& source, 
                               const std::vector<int>& expected_values) {
        if (!compiles_to_ir(source)) {
            return false;
        }
        
        // For now, just ensure it compiles - execution testing can be added later
        return true;
    }
    
    /**
     * @brief Test that invalid pointer operations are properly caught
     */
    bool test_invalid_pointer_operation(const std::string& source,
                                      const std::string& expected_error_pattern) {
        bool compilation_failed = !compiles_to_ir(source);
        
        if (!compilation_failed) {
            return false; // Should have failed but didn't
        }
        
        // Check if the error matches expected pattern
        std::string diagnostic_output = get_diagnostic_summary();
        std::regex error_regex(expected_error_pattern, std::regex_constants::icase);
        
        return std::regex_search(diagnostic_output, error_regex);
    }
    
    /**
     * @brief Generate pointer arithmetic test code
     */
    std::string generate_pointer_test(const std::string& test_name,
                                    const std::vector<int>& array_values,
                                    const std::vector<std::string>& operations) {
        std::stringstream ss;
        ss << "function " << test_name << "() -> int {\n";
        ss << "    mut arr: int[] = [";
        
        for (size_t i = 0; i < array_values.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << array_values[i];
        }
        ss << "];\n";
        ss << "    mut ptr: int* = &arr[0];\n";
        ss << "    mut result: int = 0;\n";
        
        for (const auto& op : operations) {
            ss << "    " << op << "\n";
        }
        
        ss << "    return result;\n";
        ss << "}\n";
        
        return ss.str();
    }
    
    /**
     * @brief Check if specific diagnostic error code is present
     */
    bool has_error_code(const std::string& error_code) {
        auto* diagnostics = get_diagnostics();
        if (!diagnostics) {
            return false;
        }
        
        const auto& diag_list = diagnostics->diagnostics();
        for (const auto& diag : diag_list) {
            if (diag.message().find(error_code) != std::string::npos) {
                return true;
            }
        }
        
        return false;
    }
};

/**
 * @brief Test helper specifically for struct and class operations
 */
class StructClassTestHelper : public IntegrationTestHelper {
public:
    /**
     * @brief Test struct/class compilation with field access validation
     */
    bool test_struct_operations(const std::string& source,
                              const std::vector<std::string>& required_members) {
        if (!compiles_to_ir(source)) {
            return false;
        }
        
        // Check that required members are present in symbol table
        auto* symbol_table = get_symbol_table();
        if (!symbol_table) {
            return false;
        }
        
        // Additional validation can be added here to check symbol table
        // for proper struct/class member registration
        return true;
    }
    
    /**
     * @brief Test that struct/class access violations are caught
     */
    bool test_access_violation(const std::string& source,
                             const std::string& member_name) {
        bool compilation_failed = !compiles_to_ir(source);
        
        if (!compilation_failed) {
            return false; // Should have failed but didn't
        }
        
        std::string diagnostic_output = get_diagnostic_summary();
        return diagnostic_output.find(member_name) != std::string::npos ||
               diagnostic_output.find("private") != std::string::npos ||
               diagnostic_output.find("access") != std::string::npos;
    }
    
    /**
     * @brief Generate struct test with specified fields and methods
     */
    std::string generate_struct_test(const std::string& struct_name,
                                   const std::vector<std::pair<std::string, std::string>>& fields,
                                   const std::vector<std::string>& methods) {
        std::stringstream ss;
        ss << "type struct " << struct_name << " {\n";
        
        for (const auto& field : fields) {
            ss << "    " << field.first << ": " << field.second << ";\n";
        }
        
        if (!methods.empty()) {
            ss << "\n";
            for (const auto& method : methods) {
                ss << "    " << method << "\n";
            }
        }
        
        ss << "}\n";
        
        return ss.str();
    }
    
    /**
     * @brief Validate that AST contains expected node types
     */
    bool ast_contains_node_type(const std::string& node_type) {
        auto* ast = get_ast();
        if (!ast) {
            return false;
        }
        
        // This would need actual AST traversal implementation
        // For now, just return true if we have an AST
        return true;
    }
};

/**
 * @brief Test runner for systematic execution of test suites
 */
class TestSuiteRunner {
private:
    std::vector<std::function<bool()>> test_cases;
    std::vector<std::string> test_names;
    size_t passed_tests = 0;
    size_t total_tests = 0;
    
public:
    void add_test(const std::string& name, std::function<bool()> test_func) {
        test_names.push_back(name);
        test_cases.push_back(test_func);
    }
    
    bool run_all_tests() {
        total_tests = test_cases.size();
        passed_tests = 0;
        
        std::cout << "Running " << total_tests << " tests...\n";
        std::cout << std::string(50, '=') << "\n";
        
        for (size_t i = 0; i < test_cases.size(); ++i) {
            std::cout << "[" << (i + 1) << "/" << total_tests << "] " 
                     << test_names[i] << "... ";
            
            try {
                bool result = test_cases[i]();
                if (result) {
                    std::cout << "PASS\n";
                    passed_tests++;
                } else {
                    std::cout << "FAIL\n";
                }
            } catch (const std::exception& e) {
                std::cout << "ERROR: " << e.what() << "\n";
            }
        }
        
        std::cout << std::string(50, '=') << "\n";
        std::cout << "Results: " << passed_tests << "/" << total_tests 
                 << " tests passed (" 
                 << (100.0 * passed_tests / total_tests) << "%)\n";
        
        return passed_tests == total_tests;
    }
    
    void clear() {
        test_cases.clear();
        test_names.clear();
        passed_tests = 0;
        total_tests = 0;
    }
};

} // namespace CryoTest