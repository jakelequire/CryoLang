#ifndef FILE_HPP
#define FILE_HPP

#include <string>
#include <string_view>
#include <memory>

namespace Cryo {

// ================================================================
// File Class
// ================================================================

class File {
private:
    std::string _path;
    std::string _name;
    std::string _extension;
    std::string _content;
    bool _loaded;

    // Allow factory functions to access private members
    friend std::unique_ptr<File> make_file_from_path(const std::string& path);
    friend std::unique_ptr<File> make_file_from_string(const std::string& name, const std::string& content);

public:
    // Constructors
    File(const std::string& path, const std::string& name, const std::string& extension);
    
    // Constructor for in-memory content
    static File from_content(const std::string& name, const std::string& extension, std::string content);
    
    // Destructor
    ~File() = default;
    
    // Move semantics
    File(File&&) = default;
    File& operator=(File&&) = default;
    
    // Delete copy semantics for now (can add if needed)
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    
    // File operations
    bool load();
    bool is_loaded() const { return _loaded; }
    bool close() { _loaded = false; _content.clear(); return true; }
    
    // Getters
    const std::string& path() const { return _path; }
    const std::string& name() const { return _name; }
    const std::string& extension() const { return _extension; }
    const std::string& content() const { return _content; }
    size_t size() const { return _content.size(); }
    
    // Content access
    std::string_view view() const { return _content; }
    const char* data() const { return _content.data(); }
    const char* begin() const { return _content.data(); }
    const char* end() const { return _content.data() + _content.size(); }
};

// ================================================================
// Utility Functions
// ================================================================

// Factory functions
std::unique_ptr<File> make_file_from_path(const std::string& path);
std::unique_ptr<File> make_file_from_string(const std::string& name, const std::string& content);

} // namespace Cryo

#endif // FILE_HPP