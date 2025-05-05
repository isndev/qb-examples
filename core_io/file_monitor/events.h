/**
 * @file events.h
 * @brief Event definitions for the file monitoring system
 */

#pragma once

#include <qb/actor.h>
#include <chrono>
#include <string>

namespace file_monitor {

/**
 * @brief Event types for file changes
 */
enum class FileEventType {
    CREATED,
    MODIFIED,
    DELETED,
    ATTRIBUTES_CHANGED
};

/**
 * @brief Convert file event type to string
 */
inline std::string eventTypeToString(FileEventType type) {
    switch (type) {
        case FileEventType::CREATED: return "CREATED";
        case FileEventType::MODIFIED: return "MODIFIED";
        case FileEventType::DELETED: return "DELETED";
        case FileEventType::ATTRIBUTES_CHANGED: return "ATTRIBUTES_CHANGED";
        default: return "UNKNOWN";
    }
}

/**
 * @brief File metadata structure to store file information
 */
struct FileMetadata {
    std::string path;
    size_t size = 0;
    std::string content_hash;
    std::chrono::system_clock::time_point last_modified;
};

/**
 * @brief File change event information
 */
struct FileEvent : public qb::Event {
    std::string path;
    FileEventType type;
    std::chrono::system_clock::time_point timestamp;
    
    FileEvent(const std::string& p, FileEventType t) 
        : path(p), type(t), timestamp(std::chrono::system_clock::now()) {}
};

/**
 * @brief Request to start watching a directory
 */
struct WatchDirectoryRequest : public qb::Event {
    std::string path;
    bool recursive;
    qb::ActorId requestor;
    
    WatchDirectoryRequest(const std::string& p, bool r, qb::ActorId req) 
        : path(p), recursive(r), requestor(req) {}
};

/**
 * @brief Response to a watch directory request
 */
struct WatchDirectoryResponse : public qb::Event {
    std::string path;
    bool success;
    std::string error_message;
    
    WatchDirectoryResponse(const std::string& p, bool s, const std::string& err = "") 
        : path(p), success(s), error_message(err) {}
};

/**
 * @brief Request to stop watching a directory
 */
struct UnwatchDirectoryRequest : public qb::Event {
    std::string path;
    qb::ActorId requestor;
    
    UnwatchDirectoryRequest(const std::string& p, qb::ActorId req) 
        : path(p), requestor(req) {}
};

/**
 * @brief Statistics on file monitoring
 */
struct MonitoringStats : public qb::Event {
    int directories_watched = 0;
    int files_monitored = 0;
    int created_events = 0;
    int modified_events = 0;
    int deleted_events = 0;
    int attribute_events = 0;
    
    int total_events() const {
        return created_events + modified_events + deleted_events + attribute_events;
    }
};

/**
 * @brief Statistics on file processing
 */
struct ProcessingStats : public qb::Event {
    int files_processed = 0;
    int files_created = 0;
    int files_modified = 0;
    int files_deleted = 0;
    int errors_encountered = 0;
    
    int total_operations() const {
        return files_created + files_modified + files_deleted;
    }
};

/**
 * @brief Request to update processing configuration
 */
struct SetProcessingConfigRequest : public qb::Event {
    bool process_hidden_files = false;
    
    explicit SetProcessingConfigRequest(bool process_hidden = false)
        : process_hidden_files(process_hidden) {}
};

/**
 * @brief Request to get processing statistics
 */
struct GetProcessingStatsRequest : public qb::Event {
    GetProcessingStatsRequest() = default;
};

} // namespace file_monitor 