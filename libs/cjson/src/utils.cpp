#include "../include/cjson/utils.hpp"
#include <algorithm>
#include <sstream>
#include <set>
#include <cmath>

namespace cjson
{

    // JsonPath implementation
    JsonPath::JsonPath(const std::string &path)
    {
        parse_path(path);
    }

    JsonValue *JsonPath::find(JsonValue &root)
    {
        return navigate(root, elements_);
    }

    const JsonValue *JsonPath::find(const JsonValue &root) const
    {
        return navigate(root, elements_);
    }

    bool JsonPath::set(JsonValue &root, const JsonValue &value)
    {
        if (elements_.empty())
        {
            root = value;
            return true;
        }

        std::vector<PathElement> parent_path(elements_.begin(), elements_.end() - 1);
        const PathElement &last_element = elements_.back();

        JsonValue *parent = navigate(root, parent_path, true);
        if (!parent)
        {
            return false;
        }

        try
        {
            if (last_element.type == PathElement::Key)
            {
                if (parent->is_null())
                {
                    *parent = JsonValue::object();
                }
                (*parent)[last_element.key] = value;
            }
            else
            {
                if (parent->is_null())
                {
                    *parent = JsonValue::array();
                }
                (*parent)[last_element.index] = value;
            }
            return true;
        }
        catch (const JsonException &)
        {
            return false;
        }
    }

    bool JsonPath::remove(JsonValue &root)
    {
        if (elements_.empty())
        {
            return false;
        }

        std::vector<PathElement> parent_path(elements_.begin(), elements_.end() - 1);
        const PathElement &last_element = elements_.back();

        JsonValue *parent = navigate(root, parent_path);
        if (!parent)
        {
            return false;
        }

        try
        {
            if (last_element.type == PathElement::Key && parent->is_object())
            {
                auto &obj = parent->as_object();
                return obj.erase(last_element.key) > 0;
            }
            else if (last_element.type == PathElement::Index && parent->is_array())
            {
                auto &arr = parent->as_array();
                if (last_element.index < arr.size())
                {
                    arr.erase(arr.begin() + last_element.index);
                    return true;
                }
            }
            return false;
        }
        catch (const JsonException &)
        {
            return false;
        }
    }

    bool JsonPath::exists(const JsonValue &root) const
    {
        return find(root) != nullptr;
    }

    void JsonPath::parse_path(const std::string &path)
    {
        elements_.clear();

        if (path.empty() || path == "$")
        {
            return; // Root path
        }

        size_t pos = 0;
        if (path[0] == '$')
        {
            pos = 1;
        }

        while (pos < path.length())
        {
            if (path[pos] == '.')
            {
                pos++;
                // Parse key
                size_t start = pos;
                while (pos < path.length() && path[pos] != '.' && path[pos] != '[')
                {
                    pos++;
                }
                if (pos > start)
                {
                    elements_.emplace_back(path.substr(start, pos - start));
                }
            }
            else if (path[pos] == '[')
            {
                pos++;
                size_t start = pos;
                while (pos < path.length() && path[pos] != ']')
                {
                    pos++;
                }
                if (pos < path.length())
                {
                    std::string index_str = path.substr(start, pos - start);
                    if (index_str.front() == '"' && index_str.back() == '"')
                    {
                        // String key in brackets
                        elements_.emplace_back(index_str.substr(1, index_str.length() - 2));
                    }
                    else
                    {
                        // Numeric index
                        elements_.emplace_back(static_cast<size_t>(std::stoul(index_str)));
                    }
                    pos++; // Skip ']'
                }
            }
            else
            {
                pos++;
            }
        }
    }

    JsonValue *JsonPath::navigate(JsonValue &current, const std::vector<PathElement> &path, bool create_missing)
    {
        JsonValue *node = &current;

        for (const auto &element : path)
        {
            if (element.type == PathElement::Key)
            {
                if (node->is_null() && create_missing)
                {
                    *node = JsonValue::object();
                }
                if (!node->is_object())
                {
                    return nullptr;
                }

                auto &obj = node->as_object();
                if (create_missing)
                {
                    node = &obj[element.key];
                }
                else
                {
                    auto it = obj.find(element.key);
                    if (it == obj.end())
                    {
                        return nullptr;
                    }
                    node = &it->second;
                }
            }
            else
            {
                if (node->is_null() && create_missing)
                {
                    *node = JsonValue::array();
                }
                if (!node->is_array())
                {
                    return nullptr;
                }

                auto &arr = node->as_array();
                if (element.index >= arr.size())
                {
                    if (create_missing)
                    {
                        arr.resize(element.index + 1);
                    }
                    else
                    {
                        return nullptr;
                    }
                }
                node = &arr[element.index];
            }
        }

        return node;
    }

    const JsonValue *JsonPath::navigate(const JsonValue &current, const std::vector<PathElement> &path) const
    {
        const JsonValue *node = &current;

        for (const auto &element : path)
        {
            if (element.type == PathElement::Key)
            {
                if (!node->is_object())
                {
                    return nullptr;
                }

                const auto &obj = node->as_object();
                auto it = obj.find(element.key);
                if (it == obj.end())
                {
                    return nullptr;
                }
                node = &it->second;
            }
            else
            {
                if (!node->is_array())
                {
                    return nullptr;
                }

                const auto &arr = node->as_array();
                if (element.index >= arr.size())
                {
                    return nullptr;
                }
                node = &arr[element.index];
            }
        }

        return node;
    }

    // Static convenience methods
    JsonValue *JsonPath::find(JsonValue &root, const std::string &path)
    {
        JsonPath jp(path);
        return jp.find(root);
    }

    const JsonValue *JsonPath::find(const JsonValue &root, const std::string &path)
    {
        JsonPath jp(path);
        return jp.find(root);
    }

    bool JsonPath::set(JsonValue &root, const std::string &path, const JsonValue &value)
    {
        JsonPath jp(path);
        return jp.set(root, value);
    }

    bool JsonPath::remove(JsonValue &root, const std::string &path)
    {
        JsonPath jp(path);
        return jp.remove(root);
    }

    bool JsonPath::exists(const JsonValue &root, const std::string &path)
    {
        JsonPath jp(path);
        return jp.exists(root);
    }

    // JsonValidator implementation
    JsonValidator::ValidationResult JsonValidator::validate_structure(const JsonValue &value)
    {
        ValidationResult errors;
        validate_structure_recursive(value, "$", errors);
        return errors;
    }

    void JsonValidator::validate_structure_recursive(const JsonValue &value, const std::string &path, ValidationResult &errors)
    {
        switch (value.type())
        {
        case JsonType::Object:
        {
            const auto &obj = value.as_object();
            for (const auto &pair : obj)
            {
                validate_structure_recursive(pair.second, path + "." + pair.first, errors);
            }
            break;
        }
        case JsonType::Array:
        {
            const auto &arr = value.as_array();
            for (size_t i = 0; i < arr.size(); ++i)
            {
                validate_structure_recursive(arr[i], path + "[" + std::to_string(i) + "]", errors);
            }
            break;
        }
        case JsonType::String:
        {
            // Validate UTF-8 strings
            const auto &str = value.as_string();
            for (size_t i = 0; i < str.length();)
            {
                unsigned char c = str[i];
                if (c <= 0x7F)
                {
                    i++;
                }
                else if ((c >> 5) == 0x06)
                {
                    if (i + 1 >= str.length() || (str[i + 1] & 0xC0) != 0x80)
                    {
                        errors.emplace_back(path, "Invalid UTF-8 sequence");
                        break;
                    }
                    i += 2;
                }
                else if ((c >> 4) == 0x0E)
                {
                    if (i + 2 >= str.length() ||
                        (str[i + 1] & 0xC0) != 0x80 ||
                        (str[i + 2] & 0xC0) != 0x80)
                    {
                        errors.emplace_back(path, "Invalid UTF-8 sequence");
                        break;
                    }
                    i += 3;
                }
                else if ((c >> 3) == 0x1E)
                {
                    if (i + 3 >= str.length() ||
                        (str[i + 1] & 0xC0) != 0x80 ||
                        (str[i + 2] & 0xC0) != 0x80 ||
                        (str[i + 3] & 0xC0) != 0x80)
                    {
                        errors.emplace_back(path, "Invalid UTF-8 sequence");
                        break;
                    }
                    i += 4;
                }
                else
                {
                    errors.emplace_back(path, "Invalid UTF-8 sequence");
                    break;
                }
            }
            break;
        }
        case JsonType::Number:
        {
            double num = value.as_number();
            if (!std::isfinite(num))
            {
                errors.emplace_back(path, "Number is not finite");
            }
            break;
        }
        default:
            break;
        }
    }

    bool JsonValidator::has_circular_references(const JsonValue &value)
    {
        std::vector<const void *> visited;
        return check_circular_refs(value, visited);
    }

    bool JsonValidator::check_circular_refs(const JsonValue &value, std::vector<const void *> &visited)
    {
        const void *ptr = nullptr;

        if (value.is_object())
        {
            ptr = &value.as_object();
        }
        else if (value.is_array())
        {
            ptr = &value.as_array();
        }
        else
        {
            return false;
        }

        if (std::find(visited.begin(), visited.end(), ptr) != visited.end())
        {
            return true;
        }

        visited.push_back(ptr);

        if (value.is_object())
        {
            for (const auto &pair : value.as_object())
            {
                if (check_circular_refs(pair.second, visited))
                {
                    return true;
                }
            }
        }
        else if (value.is_array())
        {
            for (const auto &element : value.as_array())
            {
                if (check_circular_refs(element, visited))
                {
                    return true;
                }
            }
        }

        visited.pop_back();
        return false;
    }

    bool JsonValidator::is_deeply_nested(const JsonValue &value, size_t max_depth)
    {
        return !check_depth(value, 0, max_depth);
    }

    bool JsonValidator::check_depth(const JsonValue &value, size_t current_depth, size_t max_depth)
    {
        if (current_depth > max_depth)
        {
            return false;
        }

        if (value.is_object())
        {
            for (const auto &pair : value.as_object())
            {
                if (!check_depth(pair.second, current_depth + 1, max_depth))
                {
                    return false;
                }
            }
        }
        else if (value.is_array())
        {
            for (const auto &element : value.as_array())
            {
                if (!check_depth(element, current_depth + 1, max_depth))
                {
                    return false;
                }
            }
        }

        return true;
    }

    // JsonUtils implementation
    JsonValue JsonUtils::deep_copy(const JsonValue &value)
    {
        return JsonValue(value); // Copy constructor handles deep copying
    }

    JsonValue JsonUtils::merge(const JsonValue &base, const JsonValue &overlay)
    {
        if (!base.is_object() || !overlay.is_object())
        {
            return overlay;
        }

        JsonObject result = base.as_object();
        const auto &overlay_obj = overlay.as_object();

        for (const auto &pair : overlay_obj)
        {
            if (result.contains(pair.first) && result[pair.first].is_object() && pair.second.is_object())
            {
                result[pair.first] = merge(result[pair.first], pair.second);
            }
            else
            {
                result[pair.first] = pair.second;
            }
        }

        return JsonValue(std::move(result));
    }

    JsonObject JsonUtils::flatten(const JsonValue &value, const std::string &separator)
    {
        JsonObject result;
        flatten_recursive(value, "", result, separator);
        return result;
    }

    void JsonUtils::flatten_recursive(const JsonValue &value, const std::string &prefix, JsonObject &result, const std::string &separator)
    {
        if (value.is_object())
        {
            const auto &obj = value.as_object();
            for (const auto &pair : obj)
            {
                std::string new_key = prefix.empty() ? pair.first : prefix + separator + pair.first;
                flatten_recursive(pair.second, new_key, result, separator);
            }
        }
        else if (value.is_array())
        {
            const auto &arr = value.as_array();
            for (size_t i = 0; i < arr.size(); ++i)
            {
                std::string new_key = prefix + "[" + std::to_string(i) + "]";
                flatten_recursive(arr[i], new_key, result, separator);
            }
        }
        else
        {
            result[prefix] = value;
        }
    }

    JsonValue JsonUtils::unflatten(const JsonObject &flat_obj, const std::string & /*separator*/)
    {
        JsonValue result = JsonValue::object();

        for (const auto &pair : flat_obj)
        {
            JsonPath::set(result, pair.first, pair.second);
        }

        return result;
    }

    std::vector<std::string> JsonUtils::find_paths(const JsonValue &value, std::function<bool(const JsonValue &)> predicate)
    {
        std::vector<std::string> results;
        find_paths_recursive(value, "$", predicate, results);
        return results;
    }

    void JsonUtils::find_paths_recursive(const JsonValue &value, const std::string &path, std::function<bool(const JsonValue &)> predicate, std::vector<std::string> &results)
    {
        if (predicate(value))
        {
            results.push_back(path);
        }

        if (value.is_object())
        {
            const auto &obj = value.as_object();
            for (const auto &pair : obj)
            {
                find_paths_recursive(pair.second, path + "." + pair.first, predicate, results);
            }
        }
        else if (value.is_array())
        {
            const auto &arr = value.as_array();
            for (size_t i = 0; i < arr.size(); ++i)
            {
                find_paths_recursive(arr[i], path + "[" + std::to_string(i) + "]", predicate, results);
            }
        }
    }

    bool JsonUtils::deep_equal(const JsonValue &a, const JsonValue &b, const CompareOptions &options)
    {
        if (a.type() != b.type())
        {
            return false;
        }

        switch (a.type())
        {
        case JsonType::Null:
            return true;
        case JsonType::Boolean:
            return a.as_boolean() == b.as_boolean();
        case JsonType::Number:
        {
            double diff = std::abs(a.as_number() - b.as_number());
            return diff <= options.number_tolerance;
        }
        case JsonType::String:
            return a.as_string() == b.as_string();
        case JsonType::Array:
        {
            const auto &arr_a = a.as_array();
            const auto &arr_b = b.as_array();

            if (arr_a.size() != arr_b.size())
            {
                return false;
            }

            if (options.ignore_array_order)
            {
                // Complex comparison ignoring order - simplified version
                std::vector<bool> matched(arr_b.size(), false);
                for (const auto &elem_a : arr_a)
                {
                    bool found = false;
                    for (size_t i = 0; i < arr_b.size(); ++i)
                    {
                        if (!matched[i] && deep_equal(elem_a, arr_b[i], options))
                        {
                            matched[i] = true;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        return false;
                }
                return true;
            }
            else
            {
                for (size_t i = 0; i < arr_a.size(); ++i)
                {
                    if (!deep_equal(arr_a[i], arr_b[i], options))
                    {
                        return false;
                    }
                }
                return true;
            }
        }
        case JsonType::Object:
        {
            const auto &obj_a = a.as_object();
            const auto &obj_b = b.as_object();

            if (!options.ignore_extra_keys && obj_a.size() != obj_b.size())
            {
                return false;
            }

            for (const auto &pair : obj_a)
            {
                auto it = obj_b.find(pair.first);
                if (it == obj_b.end())
                {
                    return false;
                }
                if (!deep_equal(pair.second, it->second, options))
                {
                    return false;
                }
            }

            return true;
        }
        default:
            return false;
        }
    }

    size_t JsonUtils::calculate_size(const JsonValue &value)
    {
        size_t size = sizeof(JsonValue);

        switch (value.type())
        {
        case JsonType::String:
            size += value.as_string().capacity();
            break;
        case JsonType::Array:
        {
            const auto &arr = value.as_array();
            size += sizeof(JsonArray) + arr.capacity() * sizeof(JsonValue);
            for (const auto &elem : arr)
            {
                size += calculate_size(elem);
            }
            break;
        }
        case JsonType::Object:
        {
            const auto &obj = value.as_object();
            size += sizeof(JsonObject);
            for (const auto &pair : obj)
            {
                size += pair.first.capacity() + calculate_size(pair.second);
            }
            break;
        }
        default:
            break;
        }

        return size;
    }

} // namespace cjson