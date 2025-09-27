#!/usr/bin/env python3
"""
CryoLang Test Framework
=======================
A comprehensive test runner for the CryoLang compiler that:
- Discovers all .cryo test files in the test-cases directory
- Compiles each file using the cryo compiler
- Tracks and reports test results (pass/fail/error)
- Provides colored output and detailed summaries
- Supports filtering tests by category or specific files
"""

import os
import sys
import subprocess
import time
import glob
from pathlib import Path
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass
from enum import Enum

class TestResult(Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    ERROR = "ERROR"
    SKIP = "SKIP"

@dataclass
class TestCase:
    name: str
    path: str
    category: str
    result: Optional[TestResult] = None
    compile_time: float = 0.0
    error_message: str = ""
    stdout: str = ""
    stderr: str = ""

class Colors:
    """ANSI color codes for terminal output"""
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    END = '\033[0m'

class CryoTestFramework:
    def __init__(self, test_dir: str = "test/test-cases", compiler_path: str = "bin/cryo"):
        self.test_dir = Path(test_dir)
        self.compiler_path = Path(compiler_path)
        self.test_cases: List[TestCase] = []
        self.results: Dict[TestResult, int] = {result: 0 for result in TestResult}
        
    def discover_tests(self, pattern: str = "*.cryo", categories: List[str] = None) -> None:
        """Discover all test files matching the pattern in specified categories"""
        self.test_cases.clear()
        
        if not self.test_dir.exists():
            print(f"{Colors.RED}Error: Test directory '{self.test_dir}' does not exist{Colors.END}")
            sys.exit(1)
            
        print(f"{Colors.CYAN}🔍 Discovering tests in {self.test_dir}...{Colors.END}")
        
        # Get all subdirectories (categories)
        category_dirs = [d for d in self.test_dir.iterdir() if d.is_dir()]
        
        if categories:
            category_dirs = [d for d in category_dirs if d.name in categories]
        
        for category_dir in sorted(category_dirs):
            category_name = category_dir.name
            test_files = list(category_dir.glob(pattern))
            
            for test_file in sorted(test_files):
                test_case = TestCase(
                    name=test_file.stem,
                    path=str(test_file),
                    category=category_name
                )
                self.test_cases.append(test_case)
                
        print(f"{Colors.GREEN}✅ Found {len(self.test_cases)} test files across {len(category_dirs)} categories{Colors.END}")
        
        # Print discovered tests by category
        current_category = None
        for test in self.test_cases:
            if test.category != current_category:
                current_category = test.category
                print(f"\n{Colors.BOLD}{Colors.BLUE}📁 {current_category}/{Colors.END}")
            print(f"  • {test.name}.cryo")
        print()
        
    def compile_test(self, test_case: TestCase) -> TestResult:
        """Compile a single test file and return the result"""
        start_time = time.time()
        
        try:
            # Run the compiler
            result = subprocess.run(
                [str(self.compiler_path), test_case.path],
                capture_output=True,
                text=True,
                timeout=30  # 30 second timeout
            )
            
            compile_time = time.time() - start_time
            test_case.compile_time = compile_time
            test_case.stdout = result.stdout
            test_case.stderr = result.stderr
            
            if result.returncode == 0:
                # Check for specific success indicators in output
                if "Compilation successful!" in result.stdout or "Type checking completed successfully" in result.stdout:
                    return TestResult.PASS
                else:
                    test_case.error_message = "Compilation succeeded but missing success indicator"
                    return TestResult.FAIL
            else:
                test_case.error_message = f"Compiler exit code: {result.returncode}"
                return TestResult.FAIL
                
        except subprocess.TimeoutExpired:
            test_case.error_message = "Compilation timeout (>30s)"
            return TestResult.ERROR
        except FileNotFoundError:
            test_case.error_message = f"Compiler not found: {self.compiler_path}"
            return TestResult.ERROR
        except Exception as e:
            test_case.error_message = f"Unexpected error: {str(e)}"
            return TestResult.ERROR
    
    def run_tests(self, verbose: bool = False, fail_fast: bool = False) -> None:
        """Run all discovered tests"""
        if not self.test_cases:
            print(f"{Colors.RED}No tests to run. Use discover_tests() first.{Colors.END}")
            return
            
        if not self.compiler_path.exists():
            print(f"{Colors.RED}Error: Compiler not found at '{self.compiler_path}'{Colors.END}")
            print(f"{Colors.YELLOW}Hint: Run 'make all' to build the compiler first{Colors.END}")
            sys.exit(1)
            
        print(f"{Colors.BOLD}{Colors.CYAN}🚀 Running {len(self.test_cases)} tests...{Colors.END}\n")
        
        total_start_time = time.time()
        
        for i, test_case in enumerate(self.test_cases, 1):
            # Progress indicator
            progress = f"[{i:3d}/{len(self.test_cases)}]"
            test_name = f"{test_case.category}/{test_case.name}"
            
            print(f"{Colors.BLUE}{progress}{Colors.END} Testing {test_name:<40} ", end="", flush=True)
            
            # Run the test
            result = self.compile_test(test_case)
            test_case.result = result
            self.results[result] += 1
            
            # Print result with color
            if result == TestResult.PASS:
                print(f"{Colors.GREEN}PASS{Colors.END} ({test_case.compile_time:.3f}s)")
            elif result == TestResult.FAIL:
                print(f"{Colors.RED}FAIL{Colors.END} ({test_case.compile_time:.3f}s)")
                if verbose:
                    print(f"    {Colors.YELLOW}Error: {test_case.error_message}{Colors.END}")
            elif result == TestResult.ERROR:
                print(f"{Colors.MAGENTA}ERROR{Colors.END}")
                if verbose:
                    print(f"    {Colors.YELLOW}Error: {test_case.error_message}{Colors.END}")
            
            # Fail fast mode
            if fail_fast and result in [TestResult.FAIL, TestResult.ERROR]:
                print(f"\n{Colors.RED}Stopping due to --fail-fast flag{Colors.END}")
                break
                
        total_time = time.time() - total_start_time
        
        # Print summary
        self.print_summary(total_time)
        
    def print_summary(self, total_time: float) -> None:
        """Print a detailed test summary"""
        print(f"\n{Colors.BOLD}{Colors.CYAN}📊 Test Summary{Colors.END}")
        print("=" * 50)
        
        # Results by category
        category_results = {}
        for test in self.test_cases:
            if test.category not in category_results:
                category_results[test.category] = {result: 0 for result in TestResult}
            if test.result:
                category_results[test.category][test.result] += 1
        
        for category, results in sorted(category_results.items()):
            total_in_category = sum(results.values())
            passed = results[TestResult.PASS]
            print(f"{Colors.BLUE}📁 {category:<15}{Colors.END} "
                  f"{Colors.GREEN}{passed:2d} passed{Colors.END}, "
                  f"{Colors.RED}{results[TestResult.FAIL]:2d} failed{Colors.END}, "
                  f"{Colors.MAGENTA}{results[TestResult.ERROR]:2d} errors{Colors.END} "
                  f"({total_in_category} total)")
        
        print("-" * 50)
        
        # Overall results
        total_tests = sum(self.results.values())
        pass_rate = (self.results[TestResult.PASS] / total_tests * 100) if total_tests > 0 else 0
        
        print(f"{Colors.BOLD}Overall Results:{Colors.END}")
        print(f"  {Colors.GREEN}✅ Passed: {self.results[TestResult.PASS]:3d}{Colors.END}")
        print(f"  {Colors.RED}❌ Failed: {self.results[TestResult.FAIL]:3d}{Colors.END}")
        print(f"  {Colors.MAGENTA}⚠️  Errors: {self.results[TestResult.ERROR]:3d}{Colors.END}")
        print(f"  {Colors.CYAN}📈 Pass Rate: {pass_rate:.1f}%{Colors.END}")
        print(f"  {Colors.BLUE}⏱️  Total Time: {total_time:.2f}s{Colors.END}")
        
        # Exit code based on results
        if self.results[TestResult.FAIL] > 0 or self.results[TestResult.ERROR] > 0:
            print(f"\n{Colors.RED}❌ Test suite failed{Colors.END}")
            sys.exit(1)
        else:
            print(f"\n{Colors.GREEN}✅ All tests passed!{Colors.END}")
            
    def print_failures(self) -> None:
        """Print detailed information about failed tests"""
        failed_tests = [test for test in self.test_cases 
                       if test.result in [TestResult.FAIL, TestResult.ERROR]]
        
        if not failed_tests:
            return
            
        print(f"\n{Colors.BOLD}{Colors.RED}❌ Failed Tests Details{Colors.END}")
        print("=" * 60)
        
        for test in failed_tests:
            print(f"\n{Colors.RED}FAILED: {test.category}/{test.name}{Colors.END}")
            print(f"  Path: {test.path}")
            print(f"  Error: {test.error_message}")
            
            if test.stderr and test.stderr.strip():
                print(f"  Stderr:")
                for line in test.stderr.strip().split('\n'):
                    print(f"    {line}")
                    
    def run_specific_test(self, test_path: str, verbose: bool = True) -> None:
        """Run a specific test file"""
        test_file = Path(test_path)
        
        if not test_file.exists():
            print(f"{Colors.RED}Error: Test file '{test_path}' does not exist{Colors.END}")
            return
            
        # Determine category from path
        relative_path = test_file.relative_to(self.test_dir) if test_file.is_relative_to(self.test_dir) else test_file
        category = relative_path.parent.name if relative_path.parent != Path('.') else 'unknown'
        
        test_case = TestCase(
            name=test_file.stem,
            path=str(test_file),
            category=category
        )
        
        print(f"{Colors.CYAN}🧪 Running single test: {test_case.category}/{test_case.name}{Colors.END}")
        
        result = self.compile_test(test_case)
        test_case.result = result
        
        # Print detailed result
        if result == TestResult.PASS:
            print(f"{Colors.GREEN}✅ PASSED{Colors.END} ({test_case.compile_time:.3f}s)")
        else:
            print(f"{Colors.RED}❌ FAILED{Colors.END}")
            print(f"Error: {test_case.error_message}")
            
        if verbose and (test_case.stdout or test_case.stderr):
            print(f"\n{Colors.BOLD}Output:{Colors.END}")
            if test_case.stdout:
                print(f"STDOUT:\n{test_case.stdout}")
            if test_case.stderr:
                print(f"STDERR:\n{test_case.stderr}")

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='CryoLang Test Framework')
    parser.add_argument('--test-dir', default='test/test-cases', 
                       help='Directory containing test cases (default: test/test-cases)')
    parser.add_argument('--compiler', default='bin/cryo',
                       help='Path to cryo compiler (default: bin/cryo)')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Verbose output showing error details')
    parser.add_argument('--fail-fast', action='store_true',
                       help='Stop on first failure')
    parser.add_argument('--categories', nargs='+', 
                       help='Run only specific categories (e.g., functions control-flow)')
    parser.add_argument('--pattern', default='*.cryo',
                       help='File pattern to match (default: *.cryo)')
    parser.add_argument('--file', 
                       help='Run a specific test file')
    parser.add_argument('--show-failures', action='store_true',
                       help='Show detailed failure information')
    
    args = parser.parse_args()
    
    framework = CryoTestFramework(args.test_dir, args.compiler)
    
    if args.file:
        framework.run_specific_test(args.file, args.verbose)
    else:
        framework.discover_tests(args.pattern, args.categories)
        framework.run_tests(args.verbose, args.fail_fast)
        
        if args.show_failures:
            framework.print_failures()

if __name__ == "__main__":
    main()