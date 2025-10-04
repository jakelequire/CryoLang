#include "../../include/JsonParser.hpp"
#include <sstream>
#include <cmath>

namespace Cryo {
namespace LSP {

std::string JsonValue::toString() const {
    switch (type_) {
        case Type::Null:
            return "null";
        case Type::Bool:
            return boolValue_ ? "true" : "false";
        case Type::Number: {
            // Check if it's a whole number and format accordingly
            if (numberValue_ == std::floor(numberValue_)) {
                return std::to_string(static_cast<long long>(numberValue_));
            } else {
                return std::to_string(numberValue_);
            }
        }
        case Type::String:
            return "\"" + stringValue_ + "\"";
        case Type::Array: {
            std::string result = "[";
            for (size_t i = 0; i < arrayValue_.size(); ++i) {
                if (i > 0) result += ",";
                result += arrayValue_[i].toString();
            }
            result += "]";
            return result;
        }
        case Type::Object: {
            std::string result = "{";
            bool first = true;
            for (const auto& pair : objectValue_) {
                if (!first) result += ",";
                result += "\"" + pair.first + "\":" + pair.second.toString();
                first = false;
            }
            result += "}";
            return result;
        }
    }
    return "null";
}

std::optional<JsonValue> JsonParser::parse(const std::string& json) {
    // Simple JSON parser implementation
    if (json.empty()) return std::nullopt;
    
    // Trim whitespace
    std::string trimmed = json;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
    
    if (trimmed.empty()) return std::nullopt;
    
    // Handle simple values first
    if (trimmed == "null") return JsonValue();
    if (trimmed == "true") return JsonValue(true);
    if (trimmed == "false") return JsonValue(false);
    
    // Try to parse as number
    if (trimmed[0] == '-' || std::isdigit(trimmed[0])) {
        try {
            double num = std::stod(trimmed);
            return JsonValue(num);
        } catch (...) {
            // Not a number, continue
        }
    }
    
    // Parse string
    if (trimmed.length() >= 2 && trimmed[0] == '"' && trimmed.back() == '"') {
        std::string str = trimmed.substr(1, trimmed.length() - 2);
        return JsonValue(str);
    }
    
    // Parse object
    if (trimmed.length() >= 2 && trimmed[0] == '{' && trimmed.back() == '}') {
        return parseObject(trimmed);
    }
    
    // Parse array
    if (trimmed.length() >= 2 && trimmed[0] == '[' && trimmed.back() == ']') {
        return parseArray(trimmed);
    }
    
    return std::nullopt;
}

std::optional<JsonValue> JsonParser::parseObject(const std::string& json) {
    JsonObject obj;
    
    // Remove outer braces
    std::string content = json.substr(1, json.length() - 2);
    if (content.empty()) return JsonValue(obj);
    
    // Simple parser - split by commas at top level
    std::vector<std::string> pairs;
    int depth = 0;
    std::string current;
    
    for (size_t i = 0; i < content.length(); ++i) {
        char c = content[i];
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') depth--;
        else if (c == ',' && depth == 0) {
            pairs.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) pairs.push_back(current);
    
    // Parse each key-value pair
    for (const auto& pair : pairs) {
        auto colonPos = findTopLevelColon(pair);
        if (colonPos == std::string::npos) continue;
        
        std::string key = pair.substr(0, colonPos);
        std::string value = pair.substr(colonPos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Remove quotes from key
        if (key.length() >= 2 && key[0] == '"' && key.back() == '"') {
            key = key.substr(1, key.length() - 2);
        }
        
        // Parse value
        auto parsedValue = parse(value);
        if (parsedValue.has_value()) {
            obj[key] = parsedValue.value();
        }
    }
    
    return JsonValue(obj);
}

std::optional<JsonValue> JsonParser::parseArray(const std::string& json) {
    JsonArray arr;
    
    // Remove outer brackets
    std::string content = json.substr(1, json.length() - 2);
    if (content.empty()) return JsonValue(arr);
    
    // Simple parser - split by commas at top level
    std::vector<std::string> elements;
    int depth = 0;
    std::string current;
    
    for (size_t i = 0; i < content.length(); ++i) {
        char c = content[i];
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') depth--;
        else if (c == ',' && depth == 0) {
            elements.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) elements.push_back(current);
    
    // Parse each element
    for (const auto& element : elements) {
        std::string trimmed = element;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        
        auto parsedValue = parse(trimmed);
        if (parsedValue.has_value()) {
            arr.push_back(parsedValue.value());
        }
    }
    
    return JsonValue(arr);
}

size_t JsonParser::findTopLevelColon(const std::string& str) {
    int depth = 0;
    bool inString = false;
    
    for (size_t i = 0; i < str.length(); ++i) {
        char c = str[i];
        
        if (c == '"' && (i == 0 || str[i-1] != '\\')) {
            inString = !inString;
        } else if (!inString) {
            if (c == '{' || c == '[') depth++;
            else if (c == '}' || c == ']') depth--;
            else if (c == ':' && depth == 0) return i;
        }
    }
    
    return std::string::npos;
}

std::string JsonParser::serialize(const JsonValue& value) {
    return value.toString();
}

} // namespace LSP
} // namespace Cryo