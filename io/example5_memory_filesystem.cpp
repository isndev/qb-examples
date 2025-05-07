/**
 * @file example5_memory_filesystem.cpp
 * @brief Example demonstrating a simple in-memory filesystem
 *
 * This example shows how to implement a basic in-memory filesystem using standard C++ containers:
 * - Creating directories
 * - Creating and writing files
 * - Reading files
 * - Listing directory contents
 * - Getting file information
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <cstring>

/**
 * A simple implementation of an in-memory file system
 */
class MemoryFS {
public:
    // File type enum
    enum class FileType {
        DIRECTORY,
        REGULAR_FILE
    };

    // File information structure
    class FileInfo {
    private:
        std::string _path;
        size_t _size;
        FileType _type;
        time_t _ctime;
        time_t _mtime;

    public:
        FileInfo(const std::string& path, size_t size, FileType type) 
            : _path(path), _size(size), _type(type) {
            _ctime = _mtime = std::time(nullptr);
        }

        const std::string& path() const { return _path; }
        size_t size() const { return _size; }
        bool is_directory() const { return _type == FileType::DIRECTORY; }
        time_t ctime() const { return _ctime; }
        time_t mtime() const { return _mtime; }
        void update_mtime() { _mtime = std::time(nullptr); }
    };

    // File entry containing both metadata and content
    struct FileEntry {
        FileInfo info;
        std::vector<uint8_t> content;
        std::unordered_map<std::string, std::shared_ptr<FileEntry>> children; // For directories

        FileEntry(const std::string& path, FileType type) 
            : info(path, 0, type) {}
    };

private:
    std::shared_ptr<FileEntry> _root;
    
    // Split path into components
    std::vector<std::string> _split_path(const std::string& path) const {
        std::vector<std::string> components;
        std::istringstream stream(path);
        std::string component;
        
        // Skip leading slash
        if (!path.empty() && path[0] == '/') {
            stream.ignore(1);
        }
        
        // Split by slashes
        while (std::getline(stream, component, '/')) {
            if (!component.empty()) {
                components.push_back(component);
            }
        }
        
        return components;
    }
    
    // Find a file entry by path
    std::shared_ptr<FileEntry> _find_entry(const std::string& path) const {
        if (path == "/" || path.empty()) {
            return _root;
        }
        
        auto components = _split_path(path);
        auto current = _root;
        
        for (const auto& component : components) {
            if (!current || !current->info.is_directory() ||
                current->children.find(component) == current->children.end()) {
                return nullptr;
            }
            current = current->children[component];
        }
        
        return current;
    }
    
    // Get parent directory of a path
    std::pair<std::shared_ptr<FileEntry>, std::string> _get_parent_and_name(const std::string& path) const {
        auto components = _split_path(path);
        
        if (components.empty()) {
            return {_root, ""};
        }
        
        std::string name = components.back();
        components.pop_back();
        
        auto current = _root;
        for (const auto& component : components) {
            if (!current || !current->info.is_directory() || 
                current->children.find(component) == current->children.end()) {
                return {nullptr, ""};
            }
            current = current->children[component];
        }
        
        return {current, name};
    }

public:
    MemoryFS() : _root(std::make_shared<FileEntry>("/", FileType::DIRECTORY)) {}
    
    // Create a directory
    bool mkdir(const std::string& path) {
        // Check if entry already exists
        if (_find_entry(path)) {
            return false;
        }
        
        // Get parent directory
        auto [parent, name] = _get_parent_and_name(path);
        if (!parent || name.empty()) {
            return false;
        }
        
        // Create directory
        auto dir = std::make_shared<FileEntry>(path, FileType::DIRECTORY);
        parent->children[name] = dir;
        
        return true;
    }
    
    // Write data to a file
    size_t write(const std::string& path, const void* data, size_t size) {
        auto entry = _find_entry(path);
        
        if (entry) {
            // File exists, check if it's a file
            if (entry->info.is_directory()) {
                return 0;
            }
            
            // Update file content
            entry->content.resize(size);
            std::memcpy(entry->content.data(), data, size);
            entry->info.update_mtime();
            
            return size;
        } else {
            // Create new file
            auto [parent, name] = _get_parent_and_name(path);
            if (!parent || name.empty()) {
                return 0;
            }
            
            // Create file entry
            auto file = std::make_shared<FileEntry>(path, FileType::REGULAR_FILE);
            file->content.resize(size);
            std::memcpy(file->content.data(), data, size);
            parent->children[name] = file;
            
            return size;
        }
    }
    
    // Read data from a file
    size_t read(const std::string& path, void* buffer, size_t size) const {
        auto entry = _find_entry(path);
        
        if (!entry || entry->info.is_directory()) {
            return 0;
        }
        
        // Calculate how much to read
        size_t bytes_to_read = std::min(size, entry->content.size());
        
        // Copy data to buffer
        std::memcpy(buffer, entry->content.data(), bytes_to_read);
        
        return bytes_to_read;
    }
    
    // Get file information
    FileInfo stat(const std::string& path) const {
        auto entry = _find_entry(path);
        
        if (!entry) {
            // Return empty info for non-existent files
            return FileInfo("", 0, FileType::REGULAR_FILE);
        }
        
        if (entry->info.is_directory()) {
            return entry->info;
        } else {
            // For files, update the size
            return FileInfo(entry->info.path(), entry->content.size(), FileType::REGULAR_FILE);
        }
    }
    
    // List directory contents
    std::vector<FileInfo> list(const std::string& path) const {
        std::vector<FileInfo> results;
        auto entry = _find_entry(path);
        
        if (!entry || !entry->info.is_directory()) {
            return results;
        }
        
        // Add all children to results
        for (const auto& [name, child] : entry->children) {
            results.push_back(child->info);
        }
        
        return results;
    }
    
    // Copy a file
    bool copy(const std::string& src_path, const std::string& dst_path) {
        auto src_entry = _find_entry(src_path);
        
        if (!src_entry || src_entry->info.is_directory()) {
            return false;
        }
        
        // Write the data to the destination
        return write(dst_path, src_entry->content.data(), src_entry->content.size()) > 0;
    }
    
    // Move/rename a file
    bool move(const std::string& src_path, const std::string& dst_path) {
        auto src_entry = _find_entry(src_path);
        
        if (!src_entry) {
            return false;
        }
        
        auto [src_parent, src_name] = _get_parent_and_name(src_path);
        auto [dst_parent, dst_name] = _get_parent_and_name(dst_path);
        
        if (!src_parent || !dst_parent || src_name.empty() || dst_name.empty()) {
            return false;
        }
        
        // Move the entry
        dst_parent->children[dst_name] = src_entry;
        src_parent->children.erase(src_name);
        
        return true;
    }
    
    // Remove a file or directory
    bool remove(const std::string& path) {
        auto [parent, name] = _get_parent_and_name(path);
        
        if (!parent || name.empty() || parent->children.find(name) == parent->children.end()) {
            return false;
        }
        
        // Remove the entry
        parent->children.erase(name);
        
        return true;
    }
};

// Helper function to display file information
void printFileInfo(const MemoryFS::FileInfo& info) {
    qb::io::cout() << "File: " << info.path() << std::endl;
    qb::io::cout() << "  Size: " << info.size() << " bytes" << std::endl;
    qb::io::cout() << "  Type: " << (info.is_directory() ? "Directory" : "File") << std::endl;
    qb::io::cout() << "  Created: " << info.ctime() << std::endl;
    qb::io::cout() << "  Modified: " << info.mtime() << std::endl;
}

// Helper function to print the contents of a directory
void printDirectoryContents(MemoryFS& filesystem, const std::string& path) {
    qb::io::cout() << "\nDirectory contents for: " << path << std::endl;
    qb::io::cout() << "----------------------------------------" << std::endl;
    
    auto entries = filesystem.list(path);
    for (const auto& entry : entries) {
        std::string entry_type = entry.is_directory() ? "[DIR]" : "[FILE]";
        qb::io::cout() << entry_type << " " << entry.path() << " (" << entry.size() << " bytes)" << std::endl;
    }
    
    qb::io::cout() << "----------------------------------------" << std::endl;
}

int main() {
    qb::io::cout() << "Memory Filesystem Example" << std::endl;
    qb::io::cout() << "=============================" << std::endl;
    
    // Create filesystem instance
    MemoryFS filesystem;
    
    // Create some directories
    filesystem.mkdir("/docs");
    filesystem.mkdir("/images");
    filesystem.mkdir("/data");
    filesystem.mkdir("/data/config");
    
    qb::io::cout() << "Created directory structure" << std::endl;
    
    // Create and write some files
    const std::string readme_content = 
        "# Memory Filesystem\n\n"
        "This is a demonstration of a simple memory filesystem.\n"
        "It provides a virtual filesystem that exists only in memory.\n";
    
    const std::string config_content = 
        "{\n"
        "  \"app_name\": \"Memory FS Demo\",\n"
        "  \"version\": \"1.0.0\",\n"
        "  \"debug_mode\": true\n"
        "}";
    
    const std::vector<uint8_t> binary_data = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  // PNG header
        0x00, 0x00, 0x00, 0x01, 0x49, 0x48, 0x44, 0x52,  // Sample data
        0xFF, 0xD8, 0xFF, 0xE0                           // More sample data
    };
    
    // Write text files
    filesystem.write("/docs/README.md", readme_content.data(), readme_content.size());
    filesystem.write("/data/config/settings.json", config_content.data(), config_content.size());
    
    // Write binary file
    filesystem.write("/images/sample.bin", binary_data.data(), binary_data.size());
    
    qb::io::cout() << "Created files in the filesystem" << std::endl;
    
    // Display the root directory contents
    printDirectoryContents(filesystem, "/");
    
    // Display the config directory contents
    printDirectoryContents(filesystem, "/data/config");
    
    // Read and display file contents
    qb::io::cout() << "\nReading file: /docs/README.md" << std::endl;
    qb::io::cout() << "----------------------------------------" << std::endl;
    
    // Get file size and create buffer
    auto readme_info = filesystem.stat("/docs/README.md");
    std::vector<char> readme_buffer(readme_info.size() + 1, 0); // +1 for null termination
    
    // Read file into buffer
    size_t bytes_read = filesystem.read("/docs/README.md", readme_buffer.data(), readme_info.size());
    
    qb::io::cout() << "Read " << bytes_read << " bytes:" << std::endl;
    qb::io::cout() << readme_buffer.data() << std::endl;
    qb::io::cout() << "----------------------------------------" << std::endl;
    
    // Get and display file information
    qb::io::cout() << "\nFile information:" << std::endl;
    printFileInfo(filesystem.stat("/docs/README.md"));
    printFileInfo(filesystem.stat("/data/config"));
    printFileInfo(filesystem.stat("/images/sample.bin"));
    
    // Demonstrate file operations
    qb::io::cout() << "\nFile operations:" << std::endl;
    
    // Copy a file
    filesystem.copy("/docs/README.md", "/docs/README.copy.md");
    qb::io::cout() << "Copied /docs/README.md to /docs/README.copy.md" << std::endl;
    
    // Move/rename a file
    filesystem.move("/docs/README.copy.md", "/data/README.md");
    qb::io::cout() << "Moved /docs/README.copy.md to /data/README.md" << std::endl;
    
    // Remove a file
    filesystem.remove("/data/README.md");
    qb::io::cout() << "Removed /data/README.md" << std::endl;
    
    // Display final directory structure
    qb::io::cout() << "\nFinal directory structure:" << std::endl;
    printDirectoryContents(filesystem, "/");
    printDirectoryContents(filesystem, "/docs");
    printDirectoryContents(filesystem, "/data");
    
    qb::io::cout() << "\nMemory filesystem example completed" << std::endl;
    
    return 0;
} 