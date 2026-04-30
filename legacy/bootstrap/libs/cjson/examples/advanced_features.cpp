#include "../include/cjson/json.hpp"
#include "../include/cjson/utils.hpp"
#include <iostream>

int main()
{
    using namespace cjson;

    std::cout << "=== Advanced JSON Features Example ===" << std::endl;

    // Create a complex nested structure
    JsonValue data = JsonValue::object({{"company", "TechCorp"},
                                        {"employees", JsonValue::array({JsonValue::object({{"id", 1},
                                                                                           {"name", "Alice Johnson"},
                                                                                           {"department", "Engineering"},
                                                                                           {"salary", 75000},
                                                                                           {"skills", JsonValue::array({"C++", "Python", "JavaScript"})}}),
                                                                        JsonValue::object({{"id", 2},
                                                                                           {"name", "Bob Smith"},
                                                                                           {"department", "Marketing"},
                                                                                           {"salary", 65000},
                                                                                           {"skills", JsonValue::array({"SEO", "Content", "Analytics"})}}),
                                                                        JsonValue::object({{"id", 3},
                                                                                           {"name", "Charlie Brown"},
                                                                                           {"department", "Engineering"},
                                                                                           {"salary", 80000},
                                                                                           {"skills", JsonValue::array({"C++", "Rust", "Docker"})}})})},
                                        {"metadata", JsonValue::object({{"created", "2024-01-01"},
                                                                        {"version", "1.0"},
                                                                        {"settings", JsonValue::object({{"debug", true},
                                                                                                        {"max_employees", 100}})}})}});

    std::cout << "\nOriginal data structure:" << std::endl;
    std::cout << data.dump(2) << std::endl;

    // 1. JSON Path navigation
    std::cout << "\n=== JSON Path Navigation ===" << std::endl;

    // Find specific values using paths
    const JsonValue *first_employee_name = JsonPath::find(data, "$.employees[0].name");
    if (first_employee_name)
    {
        std::cout << "First employee: " << first_employee_name->as_string() << std::endl;
    }

    const JsonValue *debug_setting = JsonPath::find(data, "$.metadata.settings.debug");
    if (debug_setting)
    {
        std::cout << "Debug mode: " << debug_setting->as_boolean() << std::endl;
    }

    // Check if paths exist
    bool has_hr_dept = JsonPath::exists(data, "$.employees[*].department[?(@=='HR')]");
    std::cout << "Has HR department: " << (has_hr_dept ? "yes" : "no") << std::endl;

    // Modify values using paths
    JsonPath::set(data, "$.metadata.last_updated", "2024-12-13");
    JsonPath::set(data, "$.employees[3].name", "Diana Wilson");
    JsonPath::set(data, "$.employees[3].department", "HR");

    std::cout << "Added new employee and metadata..." << std::endl;

    // 2. Flattening and unflattening
    std::cout << "\n=== JSON Flattening ===" << std::endl;

    JsonObject flattened = JsonUtils::flatten(data);
    std::cout << "Flattened structure (first 5 keys):" << std::endl;
    int count = 0;
    for (const auto &pair : flattened)
    {
        if (count++ >= 5)
            break;
        std::cout << "  " << pair.first << " = " << pair.second.dump() << std::endl;
    }

    // 3. Merging JSON objects
    std::cout << "\n=== JSON Merging ===" << std::endl;

    JsonValue additional_data = JsonValue::object({{"company", "TechCorp International"}, // This will override
                                                   {"location", "San Francisco"},         // This will be added
                                                   {"metadata", JsonValue::object({
                                                                    {"version", "1.1"},  // This will override
                                                                    {"author", "System"} // This will be added to metadata
                                                                })}});

    JsonValue merged = JsonUtils::merge(data, additional_data);
    std::cout << "After merging additional data:" << std::endl;
    std::cout << "Company: " << merged["company"].as_string() << std::endl;
    std::cout << "Location: " << merged["location"].as_string() << std::endl;
    std::cout << "Version: " << merged["metadata"]["version"].as_string() << std::endl;
    std::cout << "Author: " << merged["metadata"]["author"].as_string() << std::endl;

    // 4. Finding paths with conditions
    std::cout << "\n=== Path Finding ===" << std::endl;

    // Find all paths containing numbers greater than 70000
    auto high_salary_paths = JsonUtils::find_paths(merged, [](const JsonValue &v)
                                                   { return v.is_number() && v.as_number() > 70000; });

    std::cout << "Paths with values > 70000:" << std::endl;
    for (const auto &path : high_salary_paths)
    {
        const JsonValue *value = JsonPath::find(merged, path);
        if (value)
        {
            std::cout << "  " << path << " = " << value->as_number() << std::endl;
        }
    }

    // Find all string values containing "Engineering"
    auto engineering_paths = JsonUtils::find_paths(merged, [](const JsonValue &v)
                                                   { return v.is_string() && v.as_string().find("Engineering") != std::string::npos; });

    std::cout << "Paths containing 'Engineering':" << std::endl;
    for (const auto &path : engineering_paths)
    {
        const JsonValue *value = JsonPath::find(merged, path);
        if (value)
        {
            std::cout << "  " << path << " = " << value->as_string() << std::endl;
        }
    }

    // 5. Deep comparison with options
    std::cout << "\n=== Deep Comparison ===" << std::endl;

    JsonValue array1 = JsonValue::array({"apple", "banana", "cherry"});
    JsonValue array2 = JsonValue::array({"cherry", "apple", "banana"});

    JsonUtils::CompareOptions normal_opts;
    JsonUtils::CompareOptions ignore_order_opts;
    ignore_order_opts.ignore_array_order = true;

    bool equal_normal = JsonUtils::deep_equal(array1, array2, normal_opts);
    bool equal_ignore_order = JsonUtils::deep_equal(array1, array2, ignore_order_opts);

    std::cout << "Arrays equal (normal): " << (equal_normal ? "yes" : "no") << std::endl;
    std::cout << "Arrays equal (ignore order): " << (equal_ignore_order ? "yes" : "no") << std::endl;

    // 6. Memory and size calculation
    std::cout << "\n=== Size Analysis ===" << std::endl;

    size_t memory_usage = JsonUtils::calculate_size(merged);
    size_t element_count = JsonUtils::count_elements(merged);

    std::cout << "Estimated memory usage: " << memory_usage << " bytes" << std::endl;
    std::cout << "Total elements: " << element_count << std::endl;

    // 7. Validation
    std::cout << "\n=== Validation ===" << std::endl;

    auto validation_errors = JsonValidator::validate_structure(merged);
    if (validation_errors.empty())
    {
        std::cout << "JSON structure is valid" << std::endl;
    }
    else
    {
        std::cout << "Validation errors found:" << std::endl;
        for (const auto &error : validation_errors)
        {
            std::cout << "  " << error.path << ": " << error.message << std::endl;
        }
    }

    bool has_circular_refs = JsonValidator::has_circular_references(merged);
    bool is_deeply_nested = JsonValidator::is_deeply_nested(merged, 10);

    std::cout << "Has circular references: " << (has_circular_refs ? "yes" : "no") << std::endl;
    std::cout << "Is deeply nested (>10 levels): " << (is_deeply_nested ? "yes" : "no") << std::endl;

    return 0;
}