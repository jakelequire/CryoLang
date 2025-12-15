#include "../include/cjson/json.hpp"
#include "../include/cjson/parser.hpp"
#include "../include/cjson/writer.hpp"
#include <fstream>
#include <sstream>

namespace cjson
{

    // JsonValue implementation

    JsonValue::JsonValue(const JsonArray &arr)
        : value_(std::make_unique<JsonArray>(arr)) {}

    JsonValue::JsonValue(JsonArray &&arr)
        : value_(std::make_unique<JsonArray>(std::move(arr))) {}

    JsonValue::JsonValue(const JsonObject &obj)
        : value_(std::make_unique<JsonObject>(obj)) {}

    JsonValue::JsonValue(JsonObject &&obj)
        : value_(std::make_unique<JsonObject>(std::move(obj))) {}

    JsonValue::JsonValue(std::initializer_list<JsonValue> list)
        : value_(std::make_unique<JsonArray>(list)) {}

    JsonValue::JsonValue(std::initializer_list<std::pair<const std::string, JsonValue>> list)
        : value_(std::make_unique<JsonObject>(list)) {}

    JsonValue::JsonValue(const JsonValue &other)
    {
        switch (other.type())
        {
        case JsonType::Null:
            value_ = nullptr;
            break;
        case JsonType::Boolean:
            value_ = std::get<bool>(other.value_);
            break;
        case JsonType::Number:
            value_ = std::get<double>(other.value_);
            break;
        case JsonType::String:
            value_ = std::get<std::string>(other.value_);
            break;
        case JsonType::Array:
            value_ = std::make_unique<JsonArray>(*std::get<std::unique_ptr<JsonArray>>(other.value_));
            break;
        case JsonType::Object:
            value_ = std::make_unique<JsonObject>(*std::get<std::unique_ptr<JsonObject>>(other.value_));
            break;
        }
    }

    JsonValue &JsonValue::operator=(const JsonValue &other)
    {
        if (this != &other)
        {
            JsonValue temp(other);
            *this = std::move(temp);
        }
        return *this;
    }

    JsonType JsonValue::type() const
    {
        return static_cast<JsonType>(value_.index());
    }

    bool JsonValue::as_boolean() const
    {
        if (!is_boolean())
        {
            throw JsonTypeException("Value is not a boolean");
        }
        return get_value<bool>();
    }

    double JsonValue::as_number() const
    {
        if (!is_number())
        {
            throw JsonTypeException("Value is not a number");
        }
        return get_value<double>();
    }

    const std::string &JsonValue::as_string() const
    {
        if (!is_string())
        {
            throw JsonTypeException("Value is not a string");
        }
        return get_value<std::string>();
    }

    JsonArray &JsonValue::as_array()
    {
        if (!is_array())
        {
            throw JsonTypeException("Value is not an array");
        }
        return *get_value<std::unique_ptr<JsonArray>>();
    }

    const JsonArray &JsonValue::as_array() const
    {
        if (!is_array())
        {
            throw JsonTypeException("Value is not an array");
        }
        return *get_value<std::unique_ptr<JsonArray>>();
    }

    JsonObject &JsonValue::as_object()
    {
        if (!is_object())
        {
            throw JsonTypeException("Value is not an object");
        }
        return *get_value<std::unique_ptr<JsonObject>>();
    }

    const JsonObject &JsonValue::as_object() const
    {
        if (!is_object())
        {
            throw JsonTypeException("Value is not an object");
        }
        return *get_value<std::unique_ptr<JsonObject>>();
    }

    bool JsonValue::get_bool_or(bool default_val) const
    {
        return is_boolean() ? as_boolean() : default_val;
    }

    int JsonValue::get_int_or(int default_val) const
    {
        return is_number() ? static_cast<int>(as_number()) : default_val;
    }

    double JsonValue::get_double_or(double default_val) const
    {
        return is_number() ? as_number() : default_val;
    }

    std::string JsonValue::get_string_or(const std::string &default_val) const
    {
        return is_string() ? as_string() : default_val;
    }

    JsonValue &JsonValue::operator[](size_t index)
    {
        if (!is_array())
        {
            throw JsonTypeException("Value is not an array");
        }
        auto &arr = as_array();
        if (index >= arr.size())
        {
            arr.resize(index + 1);
        }
        return arr[index];
    }

    const JsonValue &JsonValue::operator[](size_t index) const
    {
        if (!is_array())
        {
            throw JsonTypeException("Value is not an array");
        }
        const auto &arr = as_array();
        if (index >= arr.size())
        {
            throw std::out_of_range("Array index out of bounds");
        }
        return arr[index];
    }

    JsonValue &JsonValue::operator[](const std::string &key)
    {
        if (is_null())
        {
            *this = JsonValue::object();
        }
        if (!is_object())
        {
            throw JsonTypeException("Value is not an object");
        }
        return as_object()[key];
    }

    const JsonValue &JsonValue::operator[](const std::string &key) const
    {
        if (!is_object())
        {
            throw JsonTypeException("Value is not an object");
        }
        const auto &obj = as_object();
        auto it = obj.find(key);
        if (it == obj.end())
        {
            static JsonValue null_value;
            return null_value;
        }
        return it->second;
    }

    JsonValue &JsonValue::operator[](const char *key)
    {
        return (*this)[std::string(key)];
    }

    const JsonValue &JsonValue::operator[](const char *key) const
    {
        return (*this)[std::string(key)];
    }

    bool JsonValue::operator==(const JsonValue &other) const
    {
        if (type() != other.type())
        {
            return false;
        }

        switch (type())
        {
        case JsonType::Null:
            return true;
        case JsonType::Boolean:
            return as_boolean() == other.as_boolean();
        case JsonType::Number:
            return as_number() == other.as_number();
        case JsonType::String:
            return as_string() == other.as_string();
        case JsonType::Array:
            return as_array() == other.as_array();
        case JsonType::Object:
            return as_object() == other.as_object();
        default:
            return false;
        }
    }

    std::string JsonValue::dump(int indent) const
    {
        JsonWriter writer;
        writer.set_indent(indent);
        return writer.write(*this);
    }

    void JsonValue::dump(std::ostream &os, int indent) const
    {
        JsonWriter writer;
        writer.set_indent(indent);
        writer.write(*this, os);
    }

    JsonValue JsonValue::parse(const std::string &json_str)
    {
        JsonParser parser(json_str);
        return parser.parse();
    }

    JsonValue JsonValue::parse_file(const std::string &filename)
    {
        std::ifstream file(filename);
        if (!file.is_open())
        {
            throw JsonException("Unable to open file: " + filename);
        }

        std::ostringstream content;
        content << file.rdbuf();
        return parse(content.str());
    }

    JsonValue JsonValue::array(std::initializer_list<JsonValue> values)
    {
        return JsonValue(JsonArray(values));
    }

    JsonValue JsonValue::object(std::initializer_list<std::pair<const std::string, JsonValue>> pairs)
    {
        return JsonValue(JsonObject(pairs));
    }

    // JsonObject utility methods
    std::vector<std::string> JsonObject::keys() const
    {
        std::vector<std::string> result;
        result.reserve(values_.size());
        for (const auto &pair : values_)
        {
            result.push_back(pair.first);
        }
        return result;
    }

    std::vector<JsonValue> JsonObject::values() const
    {
        std::vector<JsonValue> result;
        result.reserve(values_.size());
        for (const auto &pair : values_)
        {
            result.push_back(pair.second);
        }
        return result;
    }

    // Stream operators
    std::ostream &operator<<(std::ostream &os, const JsonValue &json)
    {
        json.dump(os);
        return os;
    }

    std::istream &operator>>(std::istream &is, JsonValue &json)
    {
        std::ostringstream buffer;
        buffer << is.rdbuf();
        json = JsonValue::parse(buffer.str());
        return is;
    }

    // JsonArray method implementations
    JsonArray::JsonArray(std::initializer_list<JsonValue> values) : values_(values) {}
    JsonArray::JsonArray(size_type count) : values_(count) {}
    JsonArray::JsonArray(size_type count, const JsonValue &value) : values_(count, value) {}

    JsonValue &JsonArray::operator[](size_type index) { return values_[index]; }
    const JsonValue &JsonArray::operator[](size_type index) const { return values_[index]; }
    JsonValue &JsonArray::at(size_type index) { return values_.at(index); }
    const JsonValue &JsonArray::at(size_type index) const { return values_.at(index); }
    JsonValue &JsonArray::front() { return values_.front(); }
    const JsonValue &JsonArray::front() const { return values_.front(); }
    JsonValue &JsonArray::back() { return values_.back(); }
    const JsonValue &JsonArray::back() const { return values_.back(); }

    JsonArray::iterator JsonArray::begin() { return values_.begin(); }
    JsonArray::const_iterator JsonArray::begin() const { return values_.begin(); }
    JsonArray::const_iterator JsonArray::cbegin() const { return values_.cbegin(); }
    JsonArray::iterator JsonArray::end() { return values_.end(); }
    JsonArray::const_iterator JsonArray::end() const { return values_.end(); }
    JsonArray::const_iterator JsonArray::cend() const { return values_.cend(); }

    bool JsonArray::empty() const { return values_.empty(); }
    JsonArray::size_type JsonArray::size() const { return values_.size(); }
    JsonArray::size_type JsonArray::max_size() const { return values_.max_size(); }
    void JsonArray::reserve(size_type new_cap) { values_.reserve(new_cap); }
    JsonArray::size_type JsonArray::capacity() const { return values_.capacity(); }
    void JsonArray::shrink_to_fit() { values_.shrink_to_fit(); }

    void JsonArray::clear() { values_.clear(); }
    JsonArray::iterator JsonArray::insert(const_iterator pos, const JsonValue &value) { return values_.insert(pos, value); }
    JsonArray::iterator JsonArray::insert(const_iterator pos, JsonValue &&value) { return values_.insert(pos, std::move(value)); }
    JsonArray::iterator JsonArray::erase(const_iterator pos) { return values_.erase(pos); }
    JsonArray::iterator JsonArray::erase(const_iterator first, const_iterator last) { return values_.erase(first, last); }
    void JsonArray::push_back(const JsonValue &value) { values_.push_back(value); }
    void JsonArray::push_back(JsonValue &&value) { values_.push_back(std::move(value)); }
    void JsonArray::pop_back() { values_.pop_back(); }
    void JsonArray::resize(size_type count) { values_.resize(count); }
    void JsonArray::resize(size_type count, const JsonValue &value) { values_.resize(count, value); }

    bool JsonArray::operator==(const JsonArray &other) const { return values_ == other.values_; }
    bool JsonArray::operator!=(const JsonArray &other) const { return !(*this == other); }

    // JsonObject method implementations
    JsonObject::JsonObject(std::initializer_list<std::pair<const std::string, JsonValue>> pairs) : values_(pairs) {}
    JsonValue &JsonObject::operator[](const std::string &key) { return values_[key]; }
    JsonValue &JsonObject::operator[](const char *key) { return values_[std::string(key)]; }
    JsonValue &JsonObject::at(const std::string &key) { return values_.at(key); }
    const JsonValue &JsonObject::at(const std::string &key) const { return values_.at(key); }

    JsonObject::iterator JsonObject::find(const std::string &key) { return values_.find(key); }
    JsonObject::const_iterator JsonObject::find(const std::string &key) const { return values_.find(key); }
    bool JsonObject::contains(const std::string &key) const { return values_.find(key) != values_.end(); }
    JsonObject::size_type JsonObject::count(const std::string &key) const { return values_.count(key); }

    JsonObject::iterator JsonObject::begin() { return values_.begin(); }
    JsonObject::const_iterator JsonObject::begin() const { return values_.begin(); }
    JsonObject::const_iterator JsonObject::cbegin() const { return values_.cbegin(); }
    JsonObject::iterator JsonObject::end() { return values_.end(); }
    JsonObject::const_iterator JsonObject::end() const { return values_.end(); }
    JsonObject::const_iterator JsonObject::cend() const { return values_.cend(); }

    bool JsonObject::empty() const { return values_.empty(); }
    JsonObject::size_type JsonObject::size() const { return values_.size(); }
    JsonObject::size_type JsonObject::max_size() const { return values_.max_size(); }

    void JsonObject::clear() { values_.clear(); }
    std::pair<JsonObject::iterator, bool> JsonObject::insert(const std::pair<const std::string, JsonValue> &pair) { return values_.insert(pair); }
    JsonObject::iterator JsonObject::erase(const_iterator pos) { return values_.erase(pos); }
    JsonObject::size_type JsonObject::erase(const std::string &key) { return values_.erase(key); }

    bool JsonObject::operator==(const JsonObject &other) const { return values_ == other.values_; }
    bool JsonObject::operator!=(const JsonObject &other) const { return !(*this == other); }

} // namespace cjson