#pragma once

#include "json.hpp"
#include <string>
#include <vector>
#include <functional>

namespace cjson
{

    // JSON Path utilities for navigating JSON structures
    class JsonPath
    {
    public:
        explicit JsonPath(const std::string &path);

        // Navigate to a value using the path
        JsonValue *find(JsonValue &root);
        const JsonValue *find(const JsonValue &root) const;

        // Set a value at the path (creates intermediate objects/arrays as needed)
        bool set(JsonValue &root, const JsonValue &value);

        // Remove a value at the path
        bool remove(JsonValue &root);

        // Check if path exists
        bool exists(const JsonValue &root) const;

        // Static convenience methods
        static JsonValue *find(JsonValue &root, const std::string &path);
        static const JsonValue *find(const JsonValue &root, const std::string &path);
        static bool set(JsonValue &root, const std::string &path, const JsonValue &value);
        static bool remove(JsonValue &root, const std::string &path);
        static bool exists(const JsonValue &root, const std::string &path);

    private:
        struct PathElement
        {
            enum Type
            {
                Key,
                Index
            };
            Type type;
            std::string key;
            size_t index;

            PathElement(const std::string &k) : type(Key), key(k), index(0) {}
            PathElement(size_t i) : type(Index), key(), index(i) {}
        };

        std::vector<PathElement> elements_;

        void parse_path(const std::string &path);
        JsonValue *navigate(JsonValue &current, const std::vector<PathElement> &path, bool create_missing = false);
        const JsonValue *navigate(const JsonValue &current, const std::vector<PathElement> &path) const;
    };

    // JSON validation utilities
    class JsonValidator
    {
    public:
        struct ValidationError
        {
            std::string path;
            std::string message;

            ValidationError(const std::string &p, const std::string &m) : path(p), message(m) {}
        };

        using ValidationResult = std::vector<ValidationError>;

        // Validate JSON structure
        static ValidationResult validate_structure(const JsonValue &value);

        // Validate against a simple schema
        static ValidationResult validate_schema(const JsonValue &value, const JsonValue &schema);

        // Check for common issues
        static bool has_circular_references(const JsonValue &value);
        static bool is_deeply_nested(const JsonValue &value, size_t max_depth = 1000);
        static ValidationResult validate_utf8_strings(const JsonValue &value);

    private:
        static void validate_structure_recursive(const JsonValue &value, const std::string &path, ValidationResult &errors);
        static void validate_schema_recursive(const JsonValue &value, const JsonValue &schema, const std::string &path, ValidationResult &errors);
        static bool check_circular_refs(const JsonValue &value, std::vector<const void *> &visited);
        static bool check_depth(const JsonValue &value, size_t current_depth, size_t max_depth);
    };

    // JSON manipulation utilities
    class JsonUtils
    {
    public:
        // Deep copy
        static JsonValue deep_copy(const JsonValue &value);

        // Merge JSON objects (second overwrites first for conflicts)
        static JsonValue merge(const JsonValue &base, const JsonValue &overlay);

        // Flatten nested JSON into dot-notation keys
        static JsonObject flatten(const JsonValue &value, const std::string &separator = ".");

        // Unflatten dot-notation keys back to nested JSON
        static JsonValue unflatten(const JsonObject &flat_obj, const std::string &separator = ".");

        // Filter object by keys
        static JsonObject filter_keys(const JsonObject &obj, const std::vector<std::string> &keys);
        static JsonObject exclude_keys(const JsonObject &obj, const std::vector<std::string> &keys);

        // Transform values
        static JsonValue transform(const JsonValue &value, std::function<JsonValue(const JsonValue &)> transformer);

        // Find all paths to values matching a predicate
        static std::vector<std::string> find_paths(const JsonValue &value, std::function<bool(const JsonValue &)> predicate);

        // Get all keys in an object recursively
        static std::vector<std::string> get_all_keys(const JsonValue &value);

        // Compare JSON values with options
        struct CompareOptions
        {
            bool ignore_array_order = false;
            bool ignore_extra_keys = false;
            double number_tolerance = 0.0;
        };

        static bool deep_equal(const JsonValue &a, const JsonValue &b, const CompareOptions &options);
        static bool deep_equal(const JsonValue &a, const JsonValue &b) { return deep_equal(a, b, CompareOptions()); }

        // Calculate size/memory usage
        static size_t calculate_size(const JsonValue &value);
        static size_t count_elements(const JsonValue &value);

    private:
        static void flatten_recursive(const JsonValue &value, const std::string &prefix, JsonObject &result, const std::string &separator);
        static void get_all_keys_recursive(const JsonValue &value, const std::string &prefix, std::vector<std::string> &keys);
        static void find_paths_recursive(const JsonValue &value, const std::string &path, std::function<bool(const JsonValue &)> predicate, std::vector<std::string> &results);
    };

    // JSON schema generation
    class JsonSchemaGenerator
    {
    public:
        struct SchemaOptions
        {
            bool infer_types = true;
            bool infer_patterns = false;
            bool generate_examples = false;
            bool strict_mode = false;
        };

        static JsonValue generate_schema(const JsonValue &value, const SchemaOptions &options);
        static JsonValue generate_schema(const JsonValue &value) { return generate_schema(value, SchemaOptions()); }
        static JsonValue generate_schema_from_multiple(const std::vector<JsonValue> &values, const SchemaOptions &options);
        static JsonValue generate_schema_from_multiple(const std::vector<JsonValue> &values) { return generate_schema_from_multiple(values, SchemaOptions()); }

    private:
        static JsonValue infer_type_schema(const JsonValue &value, const SchemaOptions &options);
        static JsonValue merge_schemas(const JsonValue &schema1, const JsonValue &schema2);
    };

    // Iterator adapters for easier traversal
    template <typename ValueType>
    class JsonIteratorAdapter
    {
    public:
        class Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = ValueType;
            using difference_type = std::ptrdiff_t;
            using pointer = ValueType *;
            using reference = ValueType &;

            Iterator(ValueType *value, const std::string &path = "")
                : current_(value), path_(path), finished_(value == nullptr) {}

            reference operator*() const { return *current_; }
            pointer operator->() const { return current_; }

            Iterator &operator++();
            Iterator operator++(int)
            {
                Iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const Iterator &other) const { return current_ == other.current_ && finished_ == other.finished_; }
            bool operator!=(const Iterator &other) const { return !(*this == other); }

            const std::string &path() const { return path_; }

        private:
            ValueType *current_;
            std::string path_;
            bool finished_;
            std::vector<std::pair<ValueType *, std::string>> stack_;

            void advance();
        };

        explicit JsonIteratorAdapter(ValueType &root) : root_(&root) {}

        Iterator begin() { return Iterator(root_); }
        Iterator end() { return Iterator(nullptr); }

    private:
        ValueType *root_;
    };

    using JsonIterator = JsonIteratorAdapter<JsonValue>;
    using ConstJsonIterator = JsonIteratorAdapter<const JsonValue>;

} // namespace cjson