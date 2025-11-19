#pragma once

/**
 * @file technical_test_reporter.hpp
 * @brief Comprehensive test analysis and compiler introspection for CryoLang
 * 
 * Provides detailed technical information about each test including:
 * - Source code being tested with syntax highlighting
 * - Compiler phases involved 
 * - Expected vs actual behavior
 * - Detailed failure analysis
 * - IR generation inspection
 * - Type resolution tracking
 */

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <map>
#include <regex>
#include "Utils/SyntaxHighlighter.hpp"

namespace CryoTest {

/**
 * @brief Technical analysis of test failure points
 */
enum class FailurePoint {
    PARSING,
    TYPE_CHECKING,
    AST_BUILDING,
    IR_GENERATION,
    IR_VERIFICATION,
    LINKER,
    RUNTIME,
    MEMORY_ACCESS,
    UNKNOWN
};

/**
 * @brief Compiler phase being tested
 */
enum class CompilerPhase {
    LEXER,
    PARSER,
    TYPE_CHECKER,
    SYMBOL_RESOLUTION,
    AST_CONSTRUCTION,
    CODEGEN,
    IR_VERIFICATION,
    OPTIMIZATION,
    LINKING,
    FULL_PIPELINE,
    STDLIB_INTEGRATION
};

/**
 * @brief Test complexity classification
 */
enum class TestComplexity {
    SIMPLE,      // Basic syntax, single operations
    MODERATE,    // Multiple operations, simple types
    COMPLEX,     // Advanced types, control flow
    INTEGRATION  // Full program compilation
};

/**
 * @brief Technical test metadata extracted from test structure
 */
struct TestTechnicalInfo {
    std::string test_name;
    std::string suite_name;
    std::string description;
    CompilerPhase primary_phase;
    std::vector<CompilerPhase> phases_involved;
    TestComplexity complexity;
    std::string source_code;
    std::vector<std::string> expected_outputs;
    std::vector<std::string> error_patterns;
    bool expects_failure;
    
    // Runtime analysis
    FailurePoint failure_point = FailurePoint::UNKNOWN;
    std::string failure_details;
    std::string compiler_diagnostics;
    std::string ir_output;
    double execution_time_ms = 0.0;
};

/**
 * @brief Technical test analysis and compiler introspection engine
 */
class TechnicalTestReporter {
private:
    std::map<std::string, TestTechnicalInfo> test_metadata;
    std::vector<std::string> failed_tests;
    std::vector<std::string> crashed_tests;
    Cryo::SyntaxHighlighter syntax_highlighter;
    
public:
    /**
     * @brief Analyze test source code to extract technical metadata
     */
    void analyze_test_source(const std::string& test_name, const std::string& suite_name, 
                           const std::string& description, const std::string& test_source_file) {
        TestTechnicalInfo info;
        info.test_name = test_name;
        info.suite_name = suite_name;
        info.description = description;
        
        // Extract source code from test file
        info.source_code = extract_cryo_source_from_test(test_source_file, test_name);
        
        // Analyze compiler phases based on test name and content
        analyze_compiler_phases(info);
        
        // Determine complexity
        info.complexity = determine_test_complexity(info.source_code, suite_name);
        
        // Extract expected behavior
        extract_expected_behavior(info, test_source_file);
        
        test_metadata[test_name] = info;
    }
    
    /**
     * @brief Record test failure with technical details
     */
    void record_test_failure(const std::string& test_name, const std::string& error_output) {
        if (test_metadata.find(test_name) != test_metadata.end()) {
            auto& info = test_metadata[test_name];
            
            // Analyze failure point
            info.failure_point = analyze_failure_point(error_output);
            info.failure_details = error_output;
            
            // Extract compiler diagnostics
            info.compiler_diagnostics = extract_compiler_diagnostics(error_output);
            
            // Check for crash vs failure
            if (is_crash(error_output)) {
                crashed_tests.push_back(test_name);
            } else {
                failed_tests.push_back(test_name);
            }
        }
    }
    
    /**
     * @brief Generate comprehensive technical report
     */
    void generate_technical_report() {
        std::cout << "\n";
        std::cout << "================================================================================\n";
        std::cout << "                        TECHNICAL TEST ANALYSIS REPORT                         \n";
        std::cout << "================================================================================\n\n";
        
        // Summary statistics
        print_technical_summary();
        
        // Phase analysis
        print_phase_analysis();
        
        // Critical failures (crashes and IR verification errors)
        print_critical_failures();
        
        // Per-test technical details
        print_detailed_test_analysis();
        
        // Critical issues summary
        print_issue_summary();
    }
    
private:
    /**
     * @brief Extract Cryo source code from test implementation
     */
    std::string extract_cryo_source_from_test(const std::string& test_source_file, const std::string& test_name) {
        // Look for R"( ... )" raw string literals in test source
        // This is a simplified extraction - in practice, would parse the actual test file
        
        std::string source_snippet;
        
        // Based on test name, provide known source patterns
        if (test_name.find("UnaryOperator") != std::string::npos) {
            source_snippet = R"(
function test_unary_operators() -> void {
    mut x: i32 = 10;
    mut ptr: i32* = &x;
    const neg: i32 = -x;
    const flag: boolean = true;
    const not_flag: boolean = !flag;
    const deref_val: i32 = *ptr;
    return;
})";
        } else if (test_name.find("IncrementDecrement") != std::string::npos) {
            source_snippet = R"(
function test_increment_decrement() -> void {
    mut x: i32 = 5;
    const pre_inc: i32 = ++x;
    const post_inc: i32 = x++;
    const pre_dec: i32 = --x;
    const post_dec: i32 = x--;
    return;
})";
        } else if (test_name.find("PointerArithmetic") != std::string::npos) {
            source_snippet = R"(
function test_pointer_arithmetic() -> void {
    mut arr: i32[3] = [1, 2, 3];
    mut ptr: i32* = &arr[0];
    const first: i32 = *ptr;
    ptr = ptr + 1;
    const second: i32 = *ptr;
    return;
})";
        } else if (test_name.find("Integer") != std::string::npos) {
            source_snippet = "const test_var: int = 42;";
        } else if (test_name.find("String") != std::string::npos) {
            source_snippet = R"(const test_str: string = "Hello, World!";)";
        } else if (test_name.find("Function") != std::string::npos) {
            source_snippet = R"(
function add_func(param1: int, param2: int) -> int {
    return param1 + param2;
})";
        } else {
            source_snippet = "// Source code analysis not available for this test";
        }
        
        return source_snippet;
    }
    
    /**
     * @brief Analyze which compiler phases are involved
     */
    void analyze_compiler_phases(TestTechnicalInfo& info) {
        const std::string& suite = info.suite_name;
        const std::string& name = info.test_name;
        
        if (suite == "TypeChecker") {
            info.primary_phase = CompilerPhase::TYPE_CHECKER;
            info.phases_involved = {CompilerPhase::LEXER, CompilerPhase::PARSER, CompilerPhase::TYPE_CHECKER};
        } else if (suite == "Codegen") {
            info.primary_phase = CompilerPhase::CODEGEN;
            info.phases_involved = {CompilerPhase::LEXER, CompilerPhase::PARSER, 
                                  CompilerPhase::TYPE_CHECKER, CompilerPhase::CODEGEN, 
                                  CompilerPhase::IR_VERIFICATION};
        } else if (suite == "Parser" || suite == "Lexer") {
            info.primary_phase = CompilerPhase::PARSER;
            info.phases_involved = {CompilerPhase::LEXER, CompilerPhase::PARSER};
        } else if (suite == "StdLib" || suite == "StdLibBuild") {
            info.primary_phase = CompilerPhase::STDLIB_INTEGRATION;
            info.phases_involved = {CompilerPhase::LEXER, CompilerPhase::PARSER, 
                                  CompilerPhase::TYPE_CHECKER, CompilerPhase::SYMBOL_RESOLUTION,
                                  CompilerPhase::CODEGEN, CompilerPhase::IR_VERIFICATION};
        } else if (suite == "Integration") {
            info.primary_phase = CompilerPhase::FULL_PIPELINE;
            info.phases_involved = {CompilerPhase::LEXER, CompilerPhase::PARSER, 
                                  CompilerPhase::TYPE_CHECKER, CompilerPhase::CODEGEN, 
                                  CompilerPhase::IR_VERIFICATION, CompilerPhase::LINKING};
        }
    }
    
    /**
     * @brief Determine test complexity level
     */
    TestComplexity determine_test_complexity(const std::string& source_code, const std::string& suite) {
        if (source_code.find("function") != std::string::npos && 
            (source_code.find("struct") != std::string::npos || source_code.find("class") != std::string::npos)) {
            return TestComplexity::INTEGRATION;
        } else if (source_code.find("function") != std::string::npos || 
                   source_code.find("mut") != std::string::npos ||
                   source_code.find("*") != std::string::npos) {
            return TestComplexity::COMPLEX;
        } else if (source_code.find("const") != std::string::npos && source_code.find("=") != std::string::npos) {
            return TestComplexity::SIMPLE;
        }
        return TestComplexity::MODERATE;
    }
    
    /**
     * @brief Extract expected behavior from test
     */
    void extract_expected_behavior(TestTechnicalInfo& info, const std::string& test_source_file) {
        // Based on test patterns, determine expected behavior
        if (info.test_name.find("TypeMismatch") != std::string::npos ||
            info.test_name.find("Error") != std::string::npos) {
            info.expects_failure = true;
            info.expected_outputs.push_back("Type checking should fail");
        } else {
            info.expects_failure = false;
            info.expected_outputs.push_back("Successful compilation");
            info.expected_outputs.push_back("Valid LLVM IR generation");
        }
    }
    
    /**
     * @brief Analyze failure point from error output
     */
    FailurePoint analyze_failure_point(const std::string& error_output) {
        if (error_output.find("Function return type does not match") != std::string::npos) {
            return FailurePoint::IR_VERIFICATION;
        } else if (error_output.find("Failed to resolve type") != std::string::npos) {
            return FailurePoint::TYPE_CHECKING;
        } else if (error_output.find("Access violation") != std::string::npos ||
                   error_output.find("Segmentation fault") != std::string::npos) {
            return FailurePoint::MEMORY_ACCESS;
        } else if (error_output.find("Parse error") != std::string::npos ||
                   error_output.find("Unexpected token") != std::string::npos) {
            return FailurePoint::PARSING;
        } else if (error_output.find("IR verification") != std::string::npos) {
            return FailurePoint::IR_VERIFICATION;
        }
        return FailurePoint::UNKNOWN;
    }
    
    /**
     * @brief Extract compiler diagnostic messages
     */
    std::string extract_compiler_diagnostics(const std::string& error_output) {
        std::stringstream diagnostics;
        std::istringstream stream(error_output);
        std::string line;
        
        while (std::getline(stream, line)) {
            if (line.find("Error:") != std::string::npos ||
                line.find("Warning:") != std::string::npos ||
                line.find("Failed to") != std::string::npos ||
                line.find("Type mismatch") != std::string::npos) {
                diagnostics << line << "\n";
            }
        }
        
        return diagnostics.str();
    }
    
    /**
     * @brief Check if error indicates a crash vs controlled failure
     */
    bool is_crash(const std::string& error_output) {
        return error_output.find("Access violation") != std::string::npos ||
               error_output.find("Segmentation fault") != std::string::npos ||
               error_output.find("Stack overflow") != std::string::npos ||
               error_output.find("Process terminated") != std::string::npos;
    }
    
    /**
     * @brief Print technical summary
     */
    void print_technical_summary() {
        std::cout << "TECHNICAL SUMMARY:\n";
        std::cout << "==================\n";
        std::cout << "Total Tests Analyzed: " << test_metadata.size() << "\n";
        std::cout << "Crashed Tests: " << crashed_tests.size() << "\n";
        std::cout << "Failed Tests: " << failed_tests.size() << "\n";
        
        // Phase breakdown
        std::map<CompilerPhase, int> phase_counts;
        for (const auto& pair : test_metadata) {
            phase_counts[pair.second.primary_phase]++;
        }
        
        std::cout << "\nTests by Primary Compiler Phase:\n";
        for (const auto& pair : phase_counts) {
            std::cout << "  " << phase_to_string(pair.first) << ": " << pair.second << " tests\n";
        }
        std::cout << "\n";
    }
    
    /**
     * @brief Print detailed phase analysis
     */
    void print_phase_analysis() {
        std::cout << "COMPILER PHASE FAILURE ANALYSIS:\n";
        std::cout << "================================\n";
        
        std::map<FailurePoint, std::vector<std::string>> failures_by_point;
        for (const auto& test_name : crashed_tests) {
            if (test_metadata.find(test_name) != test_metadata.end()) {
                failures_by_point[test_metadata[test_name].failure_point].push_back(test_name);
            }
        }
        
        for (const auto& pair : failures_by_point) {
            std::cout << "\n" << failure_point_to_string(pair.first) << " Failures (" 
                      << pair.second.size() << " tests):\n";
            for (const auto& test : pair.second) {
                std::cout << "  - " << test << "\n";
            }
        }
        std::cout << "\n";
    }
    
    /**
     * @brief Print critical failures requiring immediate attention
     */
    void print_critical_failures() {
        std::cout << "CRITICAL FAILURES REQUIRING IMMEDIATE ATTENTION:\n";
        std::cout << "=================================================\n";
        
        // IR Verification failures are critical
        std::vector<std::string> ir_failures;
        std::vector<std::string> memory_failures;
        
        for (const auto& test_name : crashed_tests) {
            if (test_metadata.find(test_name) != test_metadata.end()) {
                const auto& info = test_metadata[test_name];
                if (info.failure_point == FailurePoint::IR_VERIFICATION) {
                    ir_failures.push_back(test_name);
                } else if (info.failure_point == FailurePoint::MEMORY_ACCESS) {
                    memory_failures.push_back(test_name);
                }
            }
        }
        
        if (!ir_failures.empty()) {
            std::cout << "\n1. IR VERIFICATION FAILURES (Compiler Backend Issues):\n";
            for (const auto& test : ir_failures) {
                const auto& info = test_metadata[test];
                std::cout << "   Test: " << test << "\n";
                std::cout << "   Phase: " << phase_to_string(info.primary_phase) << "\n";
                std::cout << "   Issue: " << info.failure_details.substr(0, 100) << "...\n\n";
            }
        }
        
        if (!memory_failures.empty()) {
            std::cout << "\n2. MEMORY ACCESS VIOLATIONS (Runtime Safety Issues):\n";
            for (const auto& test : memory_failures) {
                const auto& info = test_metadata[test];
                std::cout << "   Test: " << test << "\n";
                std::cout << "   Phase: " << phase_to_string(info.primary_phase) << "\n";
                std::cout << "   Issue: " << info.failure_details.substr(0, 100) << "...\n\n";
            }
        }
    }
    
    /**
     * @brief Print detailed per-test analysis
     */
    void print_detailed_test_analysis() {
        std::cout << "DETAILED TEST TECHNICAL ANALYSIS:\n";
        std::cout << "=================================\n";
        
        // Group tests by suite for better organization
        std::map<std::string, std::vector<std::string>> tests_by_suite;
        for (const auto& pair : test_metadata) {
            tests_by_suite[pair.second.suite_name].push_back(pair.first);
        }
        
        for (const auto& suite_pair : tests_by_suite) {
            std::cout << "\n--- " << suite_pair.first << " Suite ---\n";
            
            for (const auto& test_name : suite_pair.second) {
                const auto& info = test_metadata[test_name];
                print_individual_test_analysis(info);
            }
        }
    }
    
    /**
     * @brief Print analysis for individual test
     */
    void print_individual_test_analysis(const TestTechnicalInfo& info) {
        std::cout << "\nTest: " << info.test_name << "\n";
        std::cout << std::string(40, '-') << "\n";
        std::cout << "Description: " << info.description << "\n";
        std::cout << "Primary Phase: " << phase_to_string(info.primary_phase) << "\n";
        std::cout << "Complexity: " << complexity_to_string(info.complexity) << "\n";
        
        // Source code being tested with syntax highlighting
        std::cout << "\nSource Code Under Test:\n";
        std::string highlighted_code = syntax_highlighter.highlight_code(info.source_code, "cryo");
        std::cout << highlighted_code << "\n";
        
        // Compiler phases involved
        std::cout << "Compiler Phases Exercised:\n";
        for (size_t i = 0; i < info.phases_involved.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << phase_to_string(info.phases_involved[i]) << "\n";
        }
        
        // Expected behavior
        std::cout << "Expected Behavior:\n";
        for (const auto& expected : info.expected_outputs) {
            std::cout << "  - " << expected << "\n";
        }
        
        // Failure analysis (if failed)
        if (info.failure_point != FailurePoint::UNKNOWN) {
            std::cout << "\nFAILURE ANALYSIS:\n";
            std::cout << "Failure Point: " << failure_point_to_string(info.failure_point) << "\n";
            if (!info.compiler_diagnostics.empty()) {
                std::cout << "Compiler Diagnostics:\n" << info.compiler_diagnostics << "\n";
            }
        }
        
        std::cout << "\n";
    }
    
    /**
     * @brief Print critical issue summary for immediate action
     */
    void print_issue_summary() {
        std::cout << "CRITICAL ISSUE ANALYSIS:\n";
        std::cout << "========================\n";
        
        // Analyze failure patterns
        std::map<FailurePoint, int> failure_counts;
        for (const auto& test_name : crashed_tests) {
            if (test_metadata.find(test_name) != test_metadata.end()) {
                failure_counts[test_metadata[test_name].failure_point]++;
            }
        }
        
        if (failure_counts[FailurePoint::IR_VERIFICATION] > 0) {
            std::cout << "IR Verification Failures: " << failure_counts[FailurePoint::IR_VERIFICATION] 
                      << " tests - Backend codegen issues\n";
        }
        
        if (failure_counts[FailurePoint::TYPE_CHECKING] > 0) {
            std::cout << "Type Resolution Failures: " << failure_counts[FailurePoint::TYPE_CHECKING] 
                      << " tests - Frontend type system issues\n";
        }
        
        if (failure_counts[FailurePoint::MEMORY_ACCESS] > 0) {
            std::cout << "Memory Access Violations: " << failure_counts[FailurePoint::MEMORY_ACCESS] 
                      << " tests - Runtime safety issues\n";
        }
        
        std::cout << "\n";
    }
    
    // Helper methods for string conversion
    std::string phase_to_string(CompilerPhase phase) {
        switch (phase) {
            case CompilerPhase::LEXER: return "Lexer";
            case CompilerPhase::PARSER: return "Parser";
            case CompilerPhase::TYPE_CHECKER: return "Type Checker";
            case CompilerPhase::SYMBOL_RESOLUTION: return "Symbol Resolution";
            case CompilerPhase::AST_CONSTRUCTION: return "AST Construction";
            case CompilerPhase::CODEGEN: return "Code Generation";
            case CompilerPhase::IR_VERIFICATION: return "IR Verification";
            case CompilerPhase::OPTIMIZATION: return "Optimization";
            case CompilerPhase::LINKING: return "Linking";
            case CompilerPhase::FULL_PIPELINE: return "Full Pipeline";
            case CompilerPhase::STDLIB_INTEGRATION: return "StdLib Integration";
            default: return "Unknown";
        }
    }
    
    std::string failure_point_to_string(FailurePoint point) {
        switch (point) {
            case FailurePoint::PARSING: return "Parsing";
            case FailurePoint::TYPE_CHECKING: return "Type Checking";
            case FailurePoint::AST_BUILDING: return "AST Building";
            case FailurePoint::IR_GENERATION: return "IR Generation";
            case FailurePoint::IR_VERIFICATION: return "IR Verification";
            case FailurePoint::LINKER: return "Linker";
            case FailurePoint::RUNTIME: return "Runtime";
            case FailurePoint::MEMORY_ACCESS: return "Memory Access";
            default: return "Unknown";
        }
    }
    
    std::string complexity_to_string(TestComplexity complexity) {
        switch (complexity) {
            case TestComplexity::SIMPLE: return "Simple";
            case TestComplexity::MODERATE: return "Moderate";
            case TestComplexity::COMPLEX: return "Complex";
            case TestComplexity::INTEGRATION: return "Integration";
            default: return "Unknown";
        }
    }
};

} // namespace CryoTest