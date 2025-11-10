#include "test_utils.hpp"

/**
 * @file simple_tests.cpp
 * @brief Basic tests to verify our self-contained test framework works
 */

namespace CryoTest {

// Simple test helper class
class BasicTests : public CryoTestBase {
public:
    void setup() { SetUp(); }
};

// Simple utility test class
class UtilityTests : public CryoTestBase {
public:
    void setup() { SetUp(); }
};

} // namespace CryoTest

using namespace CryoTest;

// Test our basic assertion macros
CRYO_TEST(BasicTests, AssertionTrue) {
    CRYO_ASSERT_TRUE(true);
    CRYO_ASSERT_TRUE(1 == 1);
    CRYO_ASSERT_TRUE(2 + 2 == 4);
}

CRYO_TEST(BasicTests, AssertionFalse) {
    CRYO_ASSERT_FALSE(false);
    CRYO_ASSERT_FALSE(1 == 2);
    CRYO_ASSERT_FALSE(2 + 2 == 5);
}

CRYO_TEST(BasicTests, AssertionEquality) {
    CRYO_ASSERT_EQ(42, 42);
    CRYO_ASSERT_EQ(2 + 2, 4);
    CRYO_ASSERT_EQ(std::string("hello"), std::string("hello"));
}

CRYO_TEST(BasicTests, AssertionInequality) {
    CRYO_ASSERT_NE(42, 43);
    CRYO_ASSERT_NE(2 + 2, 5);
    CRYO_ASSERT_NE(std::string("hello"), std::string("world"));
}

CRYO_TEST(BasicTests, StringEquality) {
    CRYO_ASSERT_STREQ("hello", "hello");
    CRYO_ASSERT_STREQ("test", "test");
}

// Test performance measurement
CRYO_TEST(UtilityTests, MeasurementBasic) {
    CryoTest::PerformanceMeasure perf;
    perf.start();
    
    // Do some work
    int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += i;
    }
    
    auto elapsed = perf.elapsed_ms();
    CRYO_ASSERT_TRUE(elapsed >= 0.0);
    
    std::cout << "      Performance test took: " << elapsed << "ms" << std::endl;
}

// Test basic functionality  
CRYO_TEST(UtilityTests, BasicFunctionality) {
    // Test that we can create temporary directories
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "cryo_test_basic";
    std::filesystem::create_directories(temp_dir);
    
    CRYO_ASSERT_TRUE(std::filesystem::exists(temp_dir));
    
    // Clean up
    std::filesystem::remove_all(temp_dir);
}