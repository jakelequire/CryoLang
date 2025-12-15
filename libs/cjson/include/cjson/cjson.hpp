/*
 * CJson - A Modern C++ JSON Library
 *
 * This is the main header file that includes all necessary components
 * of the CJson library. Include this file to get access to all
 * JSON functionality.
 *
 * Usage:
 *   #include <cjson/cjson.hpp>
 *
 * Features:
 *   - Full JSON parsing and serialization
 *   - Modern C++17 API with std::variant
 *   - Advanced utilities like JSON Path, validation, merging
 *   - Comprehensive error handling
 *   - High performance with move semantics
 *
 * Example:
 *   auto json = cjson::JsonValue::parse(R"({"hello": "world"})");
 *   std::cout << json["hello"].as_string() << std::endl;
 */

#pragma once

// Core JSON functionality
#include "json.hpp"

// Parser with advanced options
#include "parser.hpp"

// Writer with formatting options
#include "writer.hpp"

// Utility classes and functions
#include "utils.hpp"

// Convenience namespace alias for common usage
namespace json = cjson;