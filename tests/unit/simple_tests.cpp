#include "test_utils.hpp"

/**
 * @file simple_tests.cpp
 * @brief Basic tests to verify our self-contained test framework works
 */

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
CRYO_TEST(PerformanceTests, MeasurementBasic) {
    CryoTest::PerformanceMeasure perf;
    perf.start();
    
    // Do some work
    int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += i;
    }
    
    auto elapsed = perf.elapsed_ms();
    CRYO_ASSERT_TRUE(elapsed >= 0);
    
    std::cout << "      Performance test took: " << elapsed << "ms" << std::endl;
}

// Test file system utilities  
CRYO_TEST(UtilityTests, TestDataDirectory) {
    CryoTest::CryoTestBase test_base;
    auto test_dir = test_base.get_test_data_dir();
    
    CRYO_ASSERT_TRUE(test_dir.string().find("tests") != std::string::npos);
    CRYO_ASSERT_TRUE(test_dir.string().find("fixtures") != std::string::npos);
}

// Test logger functionality
CRYO_TEST(UtilityTests, Logger) {
    CryoTest::TestLogger logger;
    
    logger.info("This is an info message");
    logger.warning("This is a warning message");
    logger.error("This is an error message");
    
    // If we get here without throwing, logging works
    CRYO_ASSERT_TRUE(true);
}

// Test memory tracker (placeholder)
CRYO_TEST(UtilityTests, MemoryTracker) {
    auto current_mem = CryoTest::MemoryTracker::get_current_memory_usage();
    auto peak_mem = CryoTest::MemoryTracker::get_peak_memory_usage();
    
    // Currently returns 0, but test that the interface works
    CRYO_ASSERT_TRUE(current_mem >= 0);
    CRYO_ASSERT_TRUE(peak_mem >= 0);
}