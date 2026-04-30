#include "../include/cjson/json.hpp"
#include "../include/cjson/parser.hpp"
#include "../include/cjson/writer.hpp"
#include "../include/cjson/utils.hpp"
#include <iostream>
#include <cassert>
#include <sstream>
#include <fstream>
#include <algorithm>

// Simple test framework
#define TEST(name) void test_##name()
#define ASSERT(condition)                                                                                    \
    do                                                                                                       \
    {                                                                                                        \
        if (!(condition))                                                                                    \
        {                                                                                                    \
            std::cerr << "Assertion failed: " #condition " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort();                                                                                    \
        }                                                                                                    \
    } while (0)

#define ASSERT_EQ(expected, actual)                                                          \
    do                                                                                       \
    {                                                                                        \
        if ((expected) != (actual))                                                          \
        {                                                                                    \
            std::cerr << "Assertion failed: expected " << (expected) << ", got " << (actual) \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;                 \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

#define ASSERT_THROWS(exception_type, code)                                                               \
    do                                                                                                    \
    {                                                                                                     \
        bool thrown = false;                                                                              \
        try                                                                                               \
        {                                                                                                 \
            code;                                                                                         \
        }                                                                                                 \
        catch (const exception_type &)                                                                    \
        {                                                                                                 \
            thrown = true;                                                                                \
        }                                                                                                 \
        catch (...)                                                                                       \
        {                                                                                                 \
            std::cerr << "Wrong exception type thrown at " << __FILE__ << ":" << __LINE__ << std::endl;   \
            std::abort();                                                                                 \
        }                                                                                                 \
        if (!thrown)                                                                                      \
        {                                                                                                 \
            std::cerr << "Expected exception not thrown at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort();                                                                                 \
        }                                                                                                 \
    } while (0)

#define RUN_TEST(name)                             \
    do                                             \
    {                                              \
        std::cout << "Running test: " #name "..."; \
        test_##name();                             \
        std::cout << " PASSED" << std::endl;       \
    } while (0)

using namespace cjson;

// Test JsonValue basic functionality
TEST(json_value_construction)
{
    JsonValue null_val;
    ASSERT(null_val.is_null());

    JsonValue bool_val(true);
    ASSERT(bool_val.is_boolean());
    ASSERT_EQ(true, bool_val.as_boolean());

    JsonValue int_val(42);
    ASSERT(int_val.is_number());
    ASSERT_EQ(42.0, int_val.as_number());

    JsonValue double_val(3.14);
    ASSERT(double_val.is_number());
    ASSERT_EQ(3.14, double_val.as_number());

    JsonValue string_val("hello");
    ASSERT(string_val.is_string());
    ASSERT_EQ(std::string("hello"), string_val.as_string());

    JsonValue array_val = JsonValue::array({1, 2, 3});
    ASSERT(array_val.is_array());
    ASSERT_EQ(3u, array_val.as_array().size());

    JsonValue object_val = JsonValue::object({{"key", "value"}});
    ASSERT(object_val.is_object());
    ASSERT_EQ(1u, object_val.as_object().size());
}

TEST(json_value_type_errors)
{
    JsonValue val(42);

    ASSERT_THROWS(JsonTypeException, val.as_string());
    ASSERT_THROWS(JsonTypeException, val.as_array());
    ASSERT_THROWS(JsonTypeException, val.as_object());
    ASSERT_THROWS(JsonTypeException, val.as_boolean());
}

TEST(json_value_array_access)
{
    JsonValue arr = JsonValue::array({1, 2, 3});

    ASSERT_EQ(1.0, arr[size_t(0)].as_number());
    ASSERT_EQ(2.0, arr[size_t(1)].as_number());
    ASSERT_EQ(3.0, arr[size_t(2)].as_number());

    // Test array expansion
    arr[size_t(5)] = 6;
    ASSERT_EQ(6u, arr.as_array().size());
    ASSERT(arr[size_t(3)].is_null());
    ASSERT(arr[size_t(4)].is_null());
    ASSERT_EQ(6.0, arr[size_t(5)].as_number());
}

TEST(json_value_object_access)
{
    JsonValue obj = JsonValue::object({{"a", 1}, {"b", 2}});

    ASSERT_EQ(1.0, obj["a"].as_number());
    ASSERT_EQ(2.0, obj["b"].as_number());

    // Test key creation
    obj["c"] = 3;
    ASSERT_EQ(3.0, obj["c"].as_number());

    // Test null object conversion
    JsonValue null_val;
    null_val["key"] = "value";
    ASSERT(null_val.is_object());
    ASSERT_EQ(std::string("value"), null_val["key"].as_string());
}

TEST(json_value_equality)
{
    JsonValue a = JsonValue::object({{"x", 1}, {"y", 2}});
    JsonValue b = JsonValue::object({{"x", 1}, {"y", 2}});
    JsonValue c = JsonValue::object({{"x", 1}, {"y", 3}});

    ASSERT(a == b);
    ASSERT(!(a == c));
    ASSERT(a != c);
}

// Test JSON parsing
TEST(json_parser_basic)
{
    std::string json = R"({
        "null": null,
        "bool": true,
        "number": 42.5,
        "string": "hello world",
        "array": [1, 2, 3],
        "object": {"nested": "value"}
    })";

    JsonValue val = JsonValue::parse(json);
    ASSERT(val.is_object());

    const auto &obj = val.as_object();
    ASSERT(obj.at("null").is_null());
    ASSERT(obj.at("bool").is_boolean());
    ASSERT_EQ(true, obj.at("bool").as_boolean());
    ASSERT_EQ(42.5, obj.at("number").as_number());
    ASSERT_EQ(std::string("hello world"), obj.at("string").as_string());
    ASSERT(obj.at("array").is_array());
    ASSERT_EQ(3u, obj.at("array").as_array().size());
    ASSERT(obj.at("object").is_object());
    ASSERT_EQ(std::string("value"), obj.at("object")["nested"].as_string());
}

TEST(json_parser_arrays)
{
    std::string json = "[1, true, null, \"string\", [1, 2], {\"key\": \"value\"}]";

    JsonValue val = JsonValue::parse(json);
    ASSERT(val.is_array());

    const auto &arr = val.as_array();
    ASSERT_EQ(6u, arr.size());
    ASSERT_EQ(1.0, arr[size_t(0)].as_number());
    ASSERT_EQ(true, arr[size_t(1)].as_boolean());
    ASSERT(arr[size_t(2)].is_null());
    ASSERT_EQ(std::string("string"), arr[size_t(3)].as_string());
    ASSERT(arr[size_t(4)].is_array());
    ASSERT(arr[size_t(5)].is_object());
}

TEST(json_parser_strings)
{
    std::string json = R"(["simple", "with\"quotes", "with\\backslash", "with\nnewline", "with\u0041unicode"])";

    JsonValue val = JsonValue::parse(json);
    const auto &arr = val.as_array();

    ASSERT_EQ(std::string("simple"), arr[size_t(0)].as_string());
    ASSERT_EQ(std::string("with\"quotes"), arr[size_t(1)].as_string());
    ASSERT_EQ(std::string("with\\backslash"), arr[size_t(2)].as_string());
    ASSERT_EQ(std::string("with\nnewline"), arr[size_t(3)].as_string());
    ASSERT_EQ(std::string("withAunicode"), arr[size_t(4)].as_string());
}

TEST(json_parser_numbers)
{
    std::string json = "[42, -42, 3.14, -3.14, 1e10, 1E-10, -1.5e+5]";

    JsonValue val = JsonValue::parse(json);
    const auto &arr = val.as_array();

    ASSERT_EQ(42.0, arr[size_t(0)].as_number());
    ASSERT_EQ(-42.0, arr[size_t(1)].as_number());
    ASSERT_EQ(3.14, arr[size_t(2)].as_number());
    ASSERT_EQ(-3.14, arr[size_t(3)].as_number());
    ASSERT_EQ(1e10, arr[size_t(4)].as_number());
    ASSERT_EQ(1E-10, arr[size_t(5)].as_number());
    ASSERT_EQ(-1.5e+5, arr[size_t(6)].as_number());
}

TEST(json_parser_errors)
{
    ASSERT_THROWS(JsonParseException, JsonValue::parse(""));
    ASSERT_THROWS(JsonParseException, JsonValue::parse("{"));
    ASSERT_THROWS(JsonParseException, JsonValue::parse("{,}"));
    ASSERT_THROWS(JsonParseException, JsonValue::parse("[,]"));
    ASSERT_THROWS(JsonParseException, JsonValue::parse("\"unterminated"));
    ASSERT_THROWS(JsonParseException, JsonValue::parse("invalid"));
    ASSERT_THROWS(JsonParseException, JsonValue::parse("123 extra"));
}

TEST(json_parser_comments)
{
    std::string json = R"({
        // Line comment
        "key": /* block comment */ "value"
        /* multi-line
           comment */
    })";

    JsonParser parser(json);
    parser.set_allow_comments(true);

    JsonValue val = parser.parse();
    ASSERT(val.is_object());
    ASSERT_EQ(std::string("value"), val["key"].as_string());
}

TEST(json_parser_trailing_commas)
{
    std::string json = R"({
        "a": 1,
        "b": 2,
    })";

    JsonParser parser(json);
    parser.set_allow_trailing_commas(true);

    JsonValue val = parser.parse();
    ASSERT(val.is_object());
    ASSERT_EQ(2u, val.as_object().size());
}

// Test JSON writing
TEST(json_writer_basic)
{
    JsonValue val = JsonValue::object({{"null", JsonValue()},
                                       {"bool", true},
                                       {"number", 42.5},
                                       {"string", "hello"},
                                       {"array", JsonValue::array({1, 2, 3})},
                                       {"object", JsonValue::object({{"nested", "value"}})}});

    std::string json = val.dump();
    JsonValue parsed = JsonValue::parse(json);

    ASSERT(val == parsed);
}

TEST(json_writer_pretty)
{
    JsonValue val = JsonValue::object({{"array", JsonValue::array({1, 2, 3})},
                                       {"object", JsonValue::object({{"key", "value"}})}});

    std::string json = val.dump(2);

    // Should contain newlines and indentation
    ASSERT(json.find('\n') != std::string::npos);
    ASSERT(json.find("  ") != std::string::npos);
}

TEST(json_writer_options)
{
    JsonValue val = JsonValue::object({{"b", 2}, {"a", 1}});

    JsonWriter writer;
    writer.set_sort_keys(true);

    std::string json = writer.write(val);

    // Keys should be sorted
    size_t pos_a = json.find("\"a\"");
    size_t pos_b = json.find("\"b\"");
    ASSERT(pos_a < pos_b);
}

// Test JsonArray
TEST(json_array_operations)
{
    JsonArray arr;
    ASSERT(arr.empty());
    ASSERT_EQ(0u, arr.size());

    arr.push_back(1);
    arr.push_back(2);
    arr.push_back(3);

    ASSERT_EQ(3u, arr.size());
    ASSERT_EQ(1.0, arr[size_t(0)].as_number());
    ASSERT_EQ(3.0, arr.back().as_number());

    arr.pop_back();
    ASSERT_EQ(2u, arr.size());

    arr.insert(arr.begin() + 1, JsonValue("inserted"));
    ASSERT_EQ(3u, arr.size());
    ASSERT_EQ(std::string("inserted"), arr[size_t(1)].as_string());

    arr.erase(arr.begin() + 1);
    ASSERT_EQ(2u, arr.size());
    ASSERT_EQ(2.0, arr[size_t(1)].as_number());
}

TEST(json_array_iterators)
{
    JsonArray arr = {1, 2, 3, 4, 5};

    int sum = 0;
    for (const auto &val : arr)
    {
        sum += static_cast<int>(val.as_number());
    }
    ASSERT_EQ(15, sum);

    // Test range-based loop with modification
    for (auto &val : arr)
    {
        val = val.as_number() * 2;
    }

    ASSERT_EQ(2.0, arr[size_t(0)].as_number());
    ASSERT_EQ(10.0, arr[size_t(4)].as_number());
}

// Test JsonObject
TEST(json_object_operations)
{
    JsonObject obj;
    ASSERT(obj.empty());
    ASSERT_EQ(0u, obj.size());

    obj["a"] = 1;
    obj["b"] = 2;
    obj["c"] = 3;

    ASSERT_EQ(3u, obj.size());
    ASSERT(obj.contains("a"));
    ASSERT(!obj.contains("d"));

    ASSERT_EQ(1u, obj.erase("a"));
    ASSERT_EQ(0u, obj.erase("a"));
    ASSERT_EQ(2u, obj.size());

    auto keys = obj.keys();
    std::sort(keys.begin(), keys.end());
    ASSERT_EQ(std::string("b"), keys[0]);
    ASSERT_EQ(std::string("c"), keys[1]);
}

TEST(json_object_iterators)
{
    JsonObject obj = {{"a", 1}, {"b", 2}, {"c", 3}};

    int sum = 0;
    for (const auto &pair : obj)
    {
        sum += static_cast<int>(pair.second.as_number());
    }
    ASSERT_EQ(6, sum);
}

// Test JsonPath
TEST(json_path_basic)
{
    JsonValue root = JsonValue::object({{"users", JsonValue::array({JsonValue::object({{"name", "Alice"}, {"age", 30}}),
                                                                    JsonValue::object({{"name", "Bob"}, {"age", 25}})})}});

    const JsonValue *alice_name = JsonPath::find(root, "$.users[0].name");
    ASSERT(alice_name != nullptr);
    ASSERT_EQ(std::string("Alice"), alice_name->as_string());

    const JsonValue *bob_age = JsonPath::find(root, "$.users[1].age");
    ASSERT(bob_age != nullptr);
    ASSERT_EQ(25.0, bob_age->as_number());

    ASSERT(JsonPath::exists(root, "$.users[0].name"));
    ASSERT(!JsonPath::exists(root, "$.users[0].email"));
}

TEST(json_path_modification)
{
    JsonValue root = JsonValue::object();

    ASSERT(JsonPath::set(root, "$.user.name", "Charlie"));
    ASSERT(JsonPath::set(root, "$.user.age", 35));
    ASSERT(JsonPath::set(root, "$.scores[0]", 95));
    ASSERT(JsonPath::set(root, "$.scores[1]", 87));

    ASSERT_EQ(std::string("Charlie"), root["user"]["name"].as_string());
    ASSERT_EQ(35.0, root["user"]["age"].as_number());
    ASSERT_EQ(95.0, root["scores"][size_t(0)].as_number());
    ASSERT_EQ(87.0, root["scores"][size_t(1)].as_number());

    ASSERT(JsonPath::remove(root, "$.user.age"));
    ASSERT(!JsonPath::exists(root, "$.user.age"));
    ASSERT(JsonPath::exists(root, "$.user.name"));
}

// Test JsonUtils
TEST(json_utils_merge)
{
    JsonValue base = JsonValue::object({{"a", 1},
                                        {"b", JsonValue::object({{"x", 10}, {"y", 20}})}});

    JsonValue overlay = JsonValue::object({{"b", JsonValue::object({{"y", 30}, {"z", 40}})},
                                           {"c", 3}});

    JsonValue merged = JsonUtils::merge(base, overlay);

    ASSERT_EQ(1.0, merged["a"].as_number());
    ASSERT_EQ(10.0, merged["b"]["x"].as_number());
    ASSERT_EQ(30.0, merged["b"]["y"].as_number());
    ASSERT_EQ(40.0, merged["b"]["z"].as_number());
    ASSERT_EQ(3.0, merged["c"].as_number());
}

TEST(json_utils_flatten)
{
    JsonValue val = JsonValue::object({{"a", 1},
                                       {"b", JsonValue::object({{"x", 2}, {"y", 3}})},
                                       {"c", JsonValue::array({4, 5})}});

    JsonObject flattened = JsonUtils::flatten(val);

    ASSERT_EQ(1.0, flattened["a"].as_number());
    ASSERT_EQ(2.0, flattened["b.x"].as_number());
    ASSERT_EQ(3.0, flattened["b.y"].as_number());
    ASSERT_EQ(4.0, flattened["c[0]"].as_number());
    ASSERT_EQ(5.0, flattened["c[1]"].as_number());
}

TEST(json_utils_deep_equal)
{
    JsonValue a = JsonValue::array({2, 1, 3});
    JsonValue b = JsonValue::array({1, 2, 3});

    JsonUtils::CompareOptions options;
    options.ignore_array_order = true;

    ASSERT(!JsonUtils::deep_equal(a, b));         // Normal comparison
    ASSERT(JsonUtils::deep_equal(a, b, options)); // Ignoring order
}

// Test validation
TEST(json_validator_structure)
{
    JsonValue val = JsonValue::object({{"valid_string", "hello"},
                                       {"valid_number", 42.5},
                                       {"nested", JsonValue::object({{"key", "value"}})}});

    auto errors = JsonValidator::validate_structure(val);
    ASSERT(errors.empty());
}

TEST(json_validator_circular_refs)
{
    JsonValue root = JsonValue::object();

    // This creates a simple structure, no circular refs
    ASSERT(!JsonValidator::has_circular_references(root));
}

TEST(json_validator_depth)
{
    // Create deeply nested structure
    JsonValue deep = JsonValue::object();
    JsonValue *current = &deep;

    for (int i = 0; i < 100; ++i)
    {
        (*current)["level"] = JsonValue::object();
        current = &(*current)["level"];
    }

    ASSERT(JsonValidator::is_deeply_nested(deep, 50));   // Too deep (100 > 50)
    ASSERT(!JsonValidator::is_deeply_nested(deep, 150)); // Not too deep (100 < 150)
}

// Performance and stress tests
TEST(json_performance_large_array)
{
    JsonArray large_array;
    large_array.reserve(10000);

    for (int i = 0; i < 10000; ++i)
    {
        large_array.push_back(i);
    }

    JsonValue val(std::move(large_array));
    std::string json = val.dump();
    JsonValue parsed = JsonValue::parse(json);

    ASSERT_EQ(10000u, parsed.as_array().size());
    ASSERT_EQ(9999.0, parsed.as_array().back().as_number());
}

TEST(json_performance_large_object)
{
    JsonObject large_object;

    for (int i = 0; i < 1000; ++i)
    {
        large_object["key_" + std::to_string(i)] = i;
    }

    JsonValue val(std::move(large_object));
    std::string json = val.dump();
    JsonValue parsed = JsonValue::parse(json);

    ASSERT_EQ(1000u, parsed.as_object().size());
    ASSERT_EQ(999.0, parsed["key_999"].as_number());
}

// Edge cases and error handling
TEST(json_edge_cases)
{
    // Empty containers
    JsonValue empty_array = JsonValue::array();
    JsonValue empty_object = JsonValue::object();

    ASSERT_EQ("[]", empty_array.dump());
    ASSERT_EQ("{}", empty_object.dump());

    // Very long string
    std::string long_string(10000, 'x');
    JsonValue long_val(long_string);
    std::string json = long_val.dump();
    JsonValue parsed = JsonValue::parse(json);

    ASSERT_EQ(long_string, parsed.as_string());
}

TEST(json_unicode_handling)
{
    // Test actual Unicode characters in round-trip serialization
    JsonValue unicode_val("Hello 世界! 🌍 café résumé");
    std::string json = unicode_val.dump();
    JsonValue parsed = JsonValue::parse(json);

    ASSERT_EQ(unicode_val.as_string(), parsed.as_string());
}

int main()
{
    try
    {
        std::cout << "Running CJson Library Tests" << std::endl;
        std::cout << "============================" << std::endl;

        // Core functionality tests
        RUN_TEST(json_value_construction);
        RUN_TEST(json_value_type_errors);
        RUN_TEST(json_value_array_access);
        RUN_TEST(json_value_object_access);
        RUN_TEST(json_value_equality);

        // Parser tests
        RUN_TEST(json_parser_basic);
        RUN_TEST(json_parser_arrays);
        RUN_TEST(json_parser_strings);
        RUN_TEST(json_parser_numbers);
        RUN_TEST(json_parser_errors);
        RUN_TEST(json_parser_comments);
        RUN_TEST(json_parser_trailing_commas);

        // Writer tests
        RUN_TEST(json_writer_basic);
        RUN_TEST(json_writer_pretty);
        RUN_TEST(json_writer_options);

        // Container tests
        RUN_TEST(json_array_operations);
        RUN_TEST(json_array_iterators);
        RUN_TEST(json_object_operations);
        RUN_TEST(json_object_iterators);

        // Utility tests
        RUN_TEST(json_path_basic);
        RUN_TEST(json_path_modification);
        RUN_TEST(json_utils_merge);
        RUN_TEST(json_utils_flatten);
        RUN_TEST(json_utils_deep_equal);

        // Validation tests
        RUN_TEST(json_validator_structure);
        RUN_TEST(json_validator_circular_refs);
        RUN_TEST(json_validator_depth);

        // Performance tests
        RUN_TEST(json_performance_large_array);
        RUN_TEST(json_performance_large_object);

        // Edge case tests
        RUN_TEST(json_edge_cases);
        RUN_TEST(json_unicode_handling);

        std::cout << std::endl
                  << "All tests passed!" << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}