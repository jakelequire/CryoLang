#include "../include/cjson/json.hpp"
#include <iostream>

int main()
{
    using namespace cjson;

    std::cout << "=== Basic JSON Usage Example ===" << std::endl;

    // 1. Creating JSON values
    std::cout << "\n1. Creating JSON values:" << std::endl;

    JsonValue person = JsonValue::object({{"name", "Alice Johnson"},
                                          {"age", 30},
                                          {"email", "alice@example.com"},
                                          {"active", true},
                                          {"hobbies", JsonValue::array({"reading", "hiking", "programming"})},
                                          {"address", JsonValue::object({{"street", "123 Main St"},
                                                                         {"city", "Anytown"},
                                                                         {"zipcode", "12345"}})}});

    std::cout << "Created person object:" << std::endl;
    std::cout << person.dump(2) << std::endl;

    // 2. Accessing values
    std::cout << "\n2. Accessing values:" << std::endl;

    std::cout << "Name: " << person["name"].as_string() << std::endl;
    std::cout << "Age: " << person["age"].as_number() << std::endl;
    std::cout << "City: " << person["address"]["city"].as_string() << std::endl;

    // 3. Safe access with defaults
    std::cout << "\n3. Safe access with defaults:" << std::endl;

    std::string phone = person["phone"].get_string_or("Not provided");
    int salary = person["salary"].get_int_or(0);

    std::cout << "Phone: " << phone << std::endl;
    std::cout << "Salary: " << salary << std::endl;

    // 4. Iterating through arrays
    std::cout << "\n4. Hobbies:" << std::endl;

    for (const auto &hobby : person["hobbies"].as_array())
    {
        std::cout << "  - " << hobby.as_string() << std::endl;
    }

    // 5. Modifying values
    std::cout << "\n5. Modifying values:" << std::endl;

    person["age"] = 31;
    person["hobbies"].as_array().push_back("cooking");
    person["address"]["country"] = "USA";

    std::cout << "After modifications:" << std::endl;
    std::cout << person.dump(2) << std::endl;

    // 6. Type checking
    std::cout << "\n6. Type checking:" << std::endl;

    if (person["age"].is_number())
    {
        std::cout << "Age is a number: " << person["age"].as_number() << std::endl;
    }

    if (person["hobbies"].is_array())
    {
        std::cout << "Hobbies is an array with " << person["hobbies"].as_array().size() << " items" << std::endl;
    }

    // 7. Parsing from string
    std::cout << "\n7. Parsing from string:" << std::endl;

    std::string json_string = R"({
        "product": "Laptop",
        "price": 999.99,
        "in_stock": true,
        "specs": {
            "cpu": "Intel i7",
            "ram": "16GB",
            "storage": "512GB SSD"
        }
    })";

    JsonValue product = JsonValue::parse(json_string);
    std::cout << "Parsed product:" << std::endl;
    std::cout << product.dump(2) << std::endl;

    // 8. Converting back to JSON
    std::cout << "\n8. Compact JSON output:" << std::endl;
    std::cout << product.dump() << std::endl;

    return 0;
}