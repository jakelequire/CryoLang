#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace Cryo {
namespace LSP {

class JsonValue {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

private:
    Type type_;
    std::string stringValue_;
    bool boolValue_;
    double numberValue_;
    std::vector<JsonValue> arrayValue_;
    std::unordered_map<std::string, JsonValue> objectValue_;

public:
    // Constructors
    JsonValue() : type_(Type::Null) {}
    JsonValue(bool b) : type_(Type::Bool), boolValue_(b) {}
    JsonValue(int i) : type_(Type::Number), numberValue_(static_cast<double>(i)) {}
    JsonValue(double d) : type_(Type::Number), numberValue_(d) {}
    JsonValue(const char* s) : type_(Type::String), stringValue_(s) {}
    JsonValue(const std::string& s) : type_(Type::String), stringValue_(s) {}
    JsonValue(const std::vector<JsonValue>& arr) : type_(Type::Array), arrayValue_(arr) {}
    JsonValue(const std::unordered_map<std::string, JsonValue>& obj) : type_(Type::Object), objectValue_(obj) {}

    // Type checking
    Type getType() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Value accessors
    bool asBool() const { return boolValue_; }
    double asNumber() const { return numberValue_; }
    const std::string& asString() const { return stringValue_; }
    const std::vector<JsonValue>& asArray() const { return arrayValue_; }
    const std::unordered_map<std::string, JsonValue>& asObject() const { return objectValue_; }

    // Serialization
    std::string toString() const;
};

using JsonObject = std::unordered_map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

class JsonParser {
public:
    static std::optional<JsonValue> parse(const std::string& json);
    static std::string serialize(const JsonValue& value);

private:
    static std::optional<JsonValue> parseObject(const std::string& json);
    static std::optional<JsonValue> parseArray(const std::string& json);
    static size_t findTopLevelColon(const std::string& str);
};

} // namespace LSP
} // namespace Cryo