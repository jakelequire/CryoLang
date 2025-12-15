#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>
#include <iostream>
#include <exception>
#include <type_traits>
#include <initializer_list>

namespace cjson
{

    // Forward declarations
    class JsonValue;
    class JsonObject;
    class JsonArray;
    class JsonParser;
    class JsonWriter;

    // Exception classes
    class JsonException : public std::exception
    {
    public:
        explicit JsonException(const std::string &message) : message_(message) {}
        const char *what() const noexcept override { return message_.c_str(); }

    private:
        std::string message_;
    };

    class JsonParseException : public JsonException
    {
    public:
        JsonParseException(const std::string &message, size_t line = 0, size_t column = 0)
            : JsonException(message), line_(line), column_(column) {}

        size_t line() const { return line_; }
        size_t column() const { return column_; }

    private:
        size_t line_;
        size_t column_;
    };

    class JsonTypeException : public JsonException
    {
    public:
        explicit JsonTypeException(const std::string &message) : JsonException(message) {}
    };

    // JSON value types
    enum class JsonType
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    // JSON Array class
    class JsonArray
    {
    public:
        using container_type = std::vector<JsonValue>;
        using iterator = container_type::iterator;
        using const_iterator = container_type::const_iterator;
        using size_type = container_type::size_type;

        // Constructors
        JsonArray() = default;
        JsonArray(std::initializer_list<JsonValue> values);
        explicit JsonArray(size_type count);
        JsonArray(size_type count, const JsonValue &value);

        // Element access
        JsonValue &operator[](size_type index);
        const JsonValue &operator[](size_type index) const;
        JsonValue &at(size_type index);
        const JsonValue &at(size_type index) const;
        JsonValue &front();
        const JsonValue &front() const;
        JsonValue &back();
        const JsonValue &back() const;

        // Iterators
        iterator begin();
        const_iterator begin() const;
        const_iterator cbegin() const;
        iterator end();
        const_iterator end() const;
        const_iterator cend() const;

        // Capacity
        bool empty() const;
        size_type size() const;
        size_type max_size() const;
        void reserve(size_type new_cap);
        size_type capacity() const;
        void shrink_to_fit();

        // Modifiers
        void clear();
        iterator insert(const_iterator pos, const JsonValue &value);
        iterator insert(const_iterator pos, JsonValue &&value);
        iterator erase(const_iterator pos);
        iterator erase(const_iterator first, const_iterator last);
        void push_back(const JsonValue &value);
        void push_back(JsonValue &&value);
        void pop_back();
        void resize(size_type count);
        void resize(size_type count, const JsonValue &value);

        // Comparison
        bool operator==(const JsonArray &other) const;
        bool operator!=(const JsonArray &other) const;

    private:
        container_type values_;
    };

    // JSON Object class
    class JsonObject
    {
    public:
        using container_type = std::map<std::string, JsonValue>;
        using iterator = container_type::iterator;
        using const_iterator = container_type::const_iterator;
        using size_type = container_type::size_type;

        // Constructors
        JsonObject() = default;
        JsonObject(std::initializer_list<std::pair<const std::string, JsonValue>> pairs);
        JsonValue &operator[](const std::string &key);
        JsonValue &operator[](const char *key);
        JsonValue &at(const std::string &key);
        const JsonValue &at(const std::string &key) const;

        // Lookup
        iterator find(const std::string &key);
        const_iterator find(const std::string &key) const;
        bool contains(const std::string &key) const;
        size_type count(const std::string &key) const;

        // Iterators
        iterator begin();
        const_iterator begin() const;
        const_iterator cbegin() const;
        iterator end();
        const_iterator end() const;
        const_iterator cend() const;

        // Capacity
        bool empty() const;
        size_type size() const;
        size_type max_size() const;

        // Modifiers
        void clear();
        std::pair<iterator, bool> insert(const std::pair<const std::string, JsonValue> &pair);
        iterator erase(const_iterator pos);
        size_type erase(const std::string &key);

        // Comparison
        bool operator==(const JsonObject &other) const;
        bool operator!=(const JsonObject &other) const;

        // Utility methods
        std::vector<std::string> keys() const;
        std::vector<JsonValue> values() const;

    private:
        container_type values_;
    };

    // Main JSON value class
    class JsonValue
    {
    public:
        // Type alias for the underlying variant
        using variant_type = std::variant<
            std::nullptr_t,
            bool,
            double,
            std::string,
            std::unique_ptr<JsonArray>,
            std::unique_ptr<JsonObject>>;

        // Constructors
        JsonValue() : value_(nullptr) {}
        JsonValue(std::nullptr_t) : value_(nullptr) {}
        JsonValue(bool b) : value_(b) {}
        JsonValue(int i) : value_(static_cast<double>(i)) {}
        JsonValue(long l) : value_(static_cast<double>(l)) {}
        JsonValue(long long ll) : value_(static_cast<double>(ll)) {}
        JsonValue(float f) : value_(static_cast<double>(f)) {}
        JsonValue(double d) : value_(d) {}
        JsonValue(const char *str) : value_(std::string(str)) {}
        JsonValue(const std::string &str) : value_(str) {}
        JsonValue(std::string &&str) : value_(std::move(str)) {}
        JsonValue(const JsonArray &arr);
        JsonValue(JsonArray &&arr);
        JsonValue(const JsonObject &obj);
        JsonValue(JsonObject &&obj);
        JsonValue(std::initializer_list<JsonValue> list);
        JsonValue(std::initializer_list<std::pair<const std::string, JsonValue>> list);

        // Copy constructor and assignment
        JsonValue(const JsonValue &other);
        JsonValue &operator=(const JsonValue &other);

        // Move constructor and assignment
        JsonValue(JsonValue &&other) noexcept = default;
        JsonValue &operator=(JsonValue &&other) noexcept = default;

        // Destructor
        ~JsonValue() = default;

        // Type checking
        JsonType type() const;
        bool is_null() const { return type() == JsonType::Null; }
        bool is_boolean() const { return type() == JsonType::Boolean; }
        bool is_number() const { return type() == JsonType::Number; }
        bool is_string() const { return type() == JsonType::String; }
        bool is_array() const { return type() == JsonType::Array; }
        bool is_object() const { return type() == JsonType::Object; }

        // Value access (with type checking)
        bool as_boolean() const;
        double as_number() const;
        const std::string &as_string() const;
        JsonArray &as_array();
        const JsonArray &as_array() const;
        JsonObject &as_object();
        const JsonObject &as_object() const;

        // Convenience accessors (throws on wrong type)
        bool get_bool() const { return as_boolean(); }
        int get_int() const { return static_cast<int>(as_number()); }
        long get_long() const { return static_cast<long>(as_number()); }
        float get_float() const { return static_cast<float>(as_number()); }
        double get_double() const { return as_number(); }
        const std::string &get_string() const { return as_string(); }

        // Safe accessors (returns default on wrong type)
        bool get_bool_or(bool default_val) const;
        int get_int_or(int default_val) const;
        double get_double_or(double default_val) const;
        std::string get_string_or(const std::string &default_val) const;

        // Array/Object access operators
        JsonValue &operator[](size_t index);
        const JsonValue &operator[](size_t index) const;
        JsonValue &operator[](const std::string &key);
        const JsonValue &operator[](const std::string &key) const;
        JsonValue &operator[](const char *key);
        const JsonValue &operator[](const char *key) const;

        // Comparison operators
        bool operator==(const JsonValue &other) const;
        bool operator!=(const JsonValue &other) const { return !(*this == other); }

        // Serialization
        std::string dump(int indent = -1) const;
        void dump(std::ostream &os, int indent = -1) const;

        // Static factory methods
        static JsonValue parse(const std::string &json_str);
        static JsonValue parse_file(const std::string &filename);
        static JsonValue array(std::initializer_list<JsonValue> values = {});
        static JsonValue object(std::initializer_list<std::pair<const std::string, JsonValue>> pairs = {});

    private:
        variant_type value_;

        template <typename T>
        T &get_value()
        {
            try
            {
                return std::get<T>(value_);
            }
            catch (const std::bad_variant_access &)
            {
                throw JsonTypeException("Invalid type access");
            }
        }

        template <typename T>
        const T &get_value() const
        {
            try
            {
                return std::get<T>(value_);
            }
            catch (const std::bad_variant_access &)
            {
                throw JsonTypeException("Invalid type access");
            }
        }
    };

    // Stream operators
    std::ostream &operator<<(std::ostream &os, const JsonValue &json);
    std::istream &operator>>(std::istream &is, JsonValue &json);

} // namespace cjson