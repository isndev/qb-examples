/**
 * @file examples/core_io/file_monitor/events.h
 * @example File Monitoring System - Shared Event Definitions
 * @brief Defines custom `qb::Event` types and related structures for the
 * file monitoring system example.
 *
 * @details
 * This header declares events used for communication between actors in the file
 * monitoring system, as well as data structures to hold file and statistics information.
 *
 * Event Types:
 * - `FileEventType`: Enum (`CREATED`, `MODIFIED`, `DELETED`, `ATTRIBUTES_CHANGED`)
 *   to categorize file system changes.
 * - `FileMetadata`: Struct to store file path, size, content hash (simple hash in example),
 *   and last modification timestamp.
 * - `FileEvent`: A `qb::Event` sent by `DirectoryWatcher` when a file system change is detected.
 *   Contains the file path, `FileEventType`, and timestamp of the event.
 * - `WatchDirectoryRequest`: A `qb::Event` sent to `DirectoryWatcher` to request monitoring
 *   of a specific directory. Contains path, recursive flag, and requestor's `ActorId`.
 * - `WatchDirectoryResponse`: A `qb::Event` sent by `DirectoryWatcher` back to the requestor,
 *   indicating success or failure of the watch request.
 * - `UnwatchDirectoryRequest`: A `qb::Event` to stop monitoring a directory.
 * - `MonitoringStats`: A `qb::Event` (or struct) to carry statistics from `DirectoryWatcher`.
 * - `ProcessingStats`: A `qb::Event` (or struct) to carry statistics from `FileProcessor`.
 * - `SetProcessingConfigRequest`: A `qb::Event` to configure the `FileProcessor` (e.g., to process hidden files).
 * - `GetProcessingStatsRequest`: A `qb::Event` to request statistics from `FileProcessor`.
 *
 * QB Features Demonstrated:
 * - Custom `qb::Event` Creation: Defining various event structs for specific system interactions.
 * - Data Encapsulation: Events carrying relevant data for consumers.
 * - Use of `std::string`, `std::chrono` for event data.
 * - `qb::ActorId` for identifying event sources/destinations.
 */

#pragma once

#include <memory>
#include <string_view>
#include <utility>
#include <qb/actor.h>
#include <qb/string.h>
#include <chrono>
#include <string>

namespace file_monitor {

/**
 * @brief Event types for file changes
 */
enum class FileEventType { CREATED, MODIFIED, DELETED, ATTRIBUTES_CHANGED };

/**
 * @brief Convert file event type to string
 */
inline std::string
eventTypeToString(FileEventType type) {
    switch (type) {
        case FileEventType::CREATED:
            return "CREATED";
        case FileEventType::MODIFIED:
            return "MODIFIED";
        case FileEventType::DELETED:
            return "DELETED";
        case FileEventType::ATTRIBUTES_CHANGED:
            return "ATTRIBUTES_CHANGED";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief File metadata structure to store file information
 */
struct FileMetadata {
    std::string                           path;
    size_t                                size = 0;
    std::string                           content_hash;
    std::chrono::system_clock::time_point last_modified;
};

/**
 * NOTE ON EVENT PAYLOADS, which governs every event in this file: the engine relocates an event
 * with `memcpy` and never runs the source destructor, so a payload member may hold no pointer
 * into itself. On libstdc++ a SHORT std::string holds exactly that -- `_M_p` addresses its own
 * inline buffer -- so after the relocation it still points at the old storage. libc++ recomputes
 * the pointer from `this`, which is why the defect is invisible on macOS and corrupts on Linux.
 * It is NOT a cross-core-only concern: pipe growth, compaction, `reply()` and `forward()`
 * relocate same-core events too. It matters here in particular because `FileProcessor` runs on
 * core 1 while the watcher and client run on core 0, so the moment the processor subscribes to a
 * watch these events start crossing a core boundary.
 *
 * A filesystem path has no useful bound, so it is boxed behind a `std::shared_ptr` rather than
 * squeezed into a `qb::string<N>` that would truncate it in silence: the pointer is relocated,
 * the characters stay put on the heap. `FileMetadata` below is NOT an event -- it is actor-owned
 * state that the engine never relocates -- so its std::string members are fine as they are.
 */

/**
 * @brief File change event information
 */
struct FileEvent : public qb::Event {
    std::shared_ptr<std::string>          path;
    FileEventType                         type;
    std::chrono::system_clock::time_point timestamp;

    FileEvent(std::string p, FileEventType t)
        : path(std::make_shared<std::string>(std::move(p)))
        , type(t)
        , timestamp(std::chrono::system_clock::now()) {}
};

/**
 * @brief Request to start watching a directory
 */
struct WatchDirectoryRequest : public qb::Event {
    std::shared_ptr<std::string> path;
    bool                         recursive;
    qb::ActorId                  requestor;

    WatchDirectoryRequest(std::string p, bool r, qb::ActorId req)
        : path(std::make_shared<std::string>(std::move(p)))
        , recursive(r)
        , requestor(req) {}
};

/**
 * @brief Response to a watch directory request
 */
struct WatchDirectoryResponse : public qb::Event {
    std::shared_ptr<std::string> path;
    bool                         success;
    qb::string<128>              error_message;

    WatchDirectoryResponse(std::string p, bool s, std::string_view err = {})
        : path(std::make_shared<std::string>(std::move(p)))
        , success(s)
        , error_message(err) {}
};

/**
 * @brief Request to stop watching a directory
 */
struct UnwatchDirectoryRequest : public qb::Event {
    std::shared_ptr<std::string> path;
    qb::ActorId                  requestor;

    UnwatchDirectoryRequest(std::string p, qb::ActorId req)
        : path(std::make_shared<std::string>(std::move(p)))
        , requestor(req) {}
};

/**
 * @brief Statistics on file monitoring
 */
struct MonitoringStats : public qb::Event {
    int directories_watched = 0;
    int files_monitored     = 0;
    int created_events      = 0;
    int modified_events     = 0;
    int deleted_events      = 0;
    int attribute_events    = 0;

    int
    total_events() const {
        return created_events + modified_events + deleted_events + attribute_events;
    }
};

/**
 * @brief Statistics on file processing
 */
struct ProcessingStats : public qb::Event {
    int files_processed    = 0;
    int files_created      = 0;
    int files_modified     = 0;
    int files_deleted      = 0;
    int errors_encountered = 0;

    int
    total_operations() const {
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