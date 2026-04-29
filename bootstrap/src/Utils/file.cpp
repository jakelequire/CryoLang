#include "Utils/File.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>

namespace Cryo {

// ================================================================
// File Implementation
// ================================================================

File::File(const std::string& path, const std::string& name, const std::string& extension)
    : _path(path), _name(name), _extension(extension), _loaded(false) {}

File File::from_content(const std::string& name, const std::string& extension, std::string content) {
    File file("", name, extension);
    file._content = std::move(content);
    file._loaded = true;
    return file;
}

bool File::load() {
    if (_loaded || _path.empty()) {
        return _loaded;
    }
    
    try {
        std::ifstream file(_path, std::ios::binary);
        if (!file) {
            return false;
        }
        
        // Get file size
        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Read file content
        _content.resize(size);
        file.read(_content.data(), size);
        
        _loaded = true;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// ================================================================
// Factory Functions
// ================================================================

std::unique_ptr<File> make_file_from_path(const std::string& path) {
    std::filesystem::path fs_path(path);
    auto file = std::make_unique<File>(path, 
                                      fs_path.stem().string(),
                                      fs_path.extension().string());
    if (!file->load()) {
        return nullptr;
    }
    return file;
}

std::unique_ptr<File> make_file_from_string(const std::string& name, const std::string& content) {
    auto file = std::make_unique<File>("", name, ".cryo");
    file->_content = content;
    file->_loaded = true;
    return file;
}

} // namespace Cryo