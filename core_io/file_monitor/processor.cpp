/**
 * @file processor.cpp
 * @brief Implementation of file event processing classes
 */

#include "processor.h"
#include <iostream>
#include <functional>
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>
#include <qb/io/system/file.h>

namespace file_monitor {

FileProcessor::FileProcessor(const std::string& base_path)
    : _base_path(base_path) {
    // Register for message types
    registerEvent<FileEvent>(*this);
    registerEvent<SetProcessingConfigRequest>(*this);
    registerEvent<GetProcessingStatsRequest>(*this);
    registerEvent<qb::KillEvent>(*this);
}

bool FileProcessor::onInit() {
    std::cout << "FileProcessor initialized on core " << id().index() << std::endl;
    return true;
}

void FileProcessor::on(FileEvent& event) {
    std::cout << "FileProcessor received " << eventTypeToString(event.type) 
              << " event for: " << event.path << std::endl;
    
    // Process based on event type
    switch (event.type) {
        case FileEventType::CREATED:
            processFileCreated(event.path);
            break;
            
        case FileEventType::MODIFIED:
            processFileModified(event.path);
            break;
            
        case FileEventType::DELETED:
            processFileDeleted(event.path);
            break;
            
        case FileEventType::ATTRIBUTES_CHANGED:
            // We don't need special handling for attribute changes
            break;
    }
    
    // Update statistics
    updateStats(event.type);
}

void FileProcessor::on(SetProcessingConfigRequest& request) {
    _process_hidden_files = request.process_hidden_files;
    std::cout << "FileProcessor: Updated configuration - processing hidden files: " 
              << (_process_hidden_files ? "enabled" : "disabled") << std::endl;
}

void FileProcessor::on(GetProcessingStatsRequest& request) {
    std::cout << "FileProcessor: Stats request received" << std::endl;
    std::cout << "  Files processed: " << _stats.files_processed << std::endl;
    std::cout << "  Files created: " << _stats.files_created << std::endl;
    std::cout << "  Files modified: " << _stats.files_modified << std::endl;
    std::cout << "  Files deleted: " << _stats.files_deleted << std::endl;
    std::cout << "  Errors: " << _stats.errors_encountered << std::endl;
}

void FileProcessor::on(qb::KillEvent&) {
    std::cout << "FileProcessor shutting down" << std::endl;
    kill();
}

void FileProcessor::processFileCreated(const std::string& path) {
    if (!shouldProcessFile(path)) {
        return;
    }
    
    try {
        FileMetadata metadata = extractMetadata(path);
        _tracked_files[path] = metadata;
        
        std::cout << "Processed new file: " << path 
                  << " (" << metadata.size << " bytes)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error processing created file: " << e.what() << std::endl;
        _stats.errors_encountered++;
    }
}

void FileProcessor::processFileModified(const std::string& path) {
    if (!shouldProcessFile(path)) {
        return;
    }
    
    try {
        // Check if we have seen this file before
        auto it = _tracked_files.find(path);
        if (it != _tracked_files.end()) {
            FileMetadata old_metadata = it->second;
            FileMetadata new_metadata = extractMetadata(path);
            
            // Check if content actually changed
            if (new_metadata.content_hash != old_metadata.content_hash) {
                std::cout << "File content changed: " << path << std::endl;
                std::cout << "  Old size: " << old_metadata.size 
                          << ", New size: " << new_metadata.size << std::endl;
                
                // Update tracked files
                _tracked_files[path] = new_metadata;
            } else {
                std::cout << "File modified but content hash unchanged: " << path << std::endl;
            }
        } else {
            // New file for us, treat as creation
            processFileCreated(path);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing modified file: " << e.what() << std::endl;
        _stats.errors_encountered++;
    }
}

void FileProcessor::processFileDeleted(const std::string& path) {
    // Remove from tracked files
    auto it = _tracked_files.find(path);
    if (it != _tracked_files.end()) {
        std::cout << "Removed deleted file from tracking: " << path << std::endl;
        _tracked_files.erase(it);
    }
}

bool FileProcessor::shouldProcessFile(const std::string& path) {
    // Skip directories
    if (fs::is_directory(path)) {
        return false;
    }
    
    // Skip hidden files if not configured to process them
    if (!_process_hidden_files) {
        std::string filename = fs::path(path).filename().string();
        if (!filename.empty() && filename[0] == '.') {
            return false;
        }
    }
    
    return true;
}

FileMetadata FileProcessor::extractMetadata(const std::string& path) {
    FileMetadata metadata;
    metadata.path = path;
    
    // Convert filesystem time to system_clock time
    auto ftime = fs::last_write_time(path);
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                 std::chrono::time_point<std::chrono::system_clock>(
                   std::chrono::duration_cast<std::chrono::system_clock::duration>(
                     ftime.time_since_epoch())));
    metadata.last_modified = sctp;
    
    // Get file size
    metadata.size = fs::file_size(path);
    
    // Read file and calculate hash
    std::vector<char> content;
    qb::io::sys::file file;
    
    if (file.open(path.c_str(), O_RDONLY) >= 0) {
        content.resize(metadata.size);
        ssize_t bytes_read = file.read(content.data(), metadata.size);
        file.close();
        
        if (bytes_read >= 0) {
            // Calculate simple hash
            size_t hash = 0;
            for (char c : content) {
                hash = hash * 31 + static_cast<unsigned char>(c);
            }
            metadata.content_hash = std::to_string(hash);
        }
    }
    
    return metadata;
}

void FileProcessor::updateStats(FileEventType event_type) {
    _stats.files_processed++;
    
    switch (event_type) {
        case FileEventType::CREATED:
            _stats.files_created++;
            break;
        case FileEventType::MODIFIED:
            _stats.files_modified++;
            break;
        case FileEventType::DELETED:
            _stats.files_deleted++;
            break;
        default:
            break;
    }
}

} // namespace file_monitor 