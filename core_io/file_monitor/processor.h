/**
 * @file examples/core_io/file_monitor/processor.h
 * @example File Monitoring System - File Processor Actor Definition
 * @brief Defines the `FileProcessor` actor, which is responsible for handling
 *        and processing file events reported by the `DirectoryWatcher`.
 *
 * @details
 * The `FileProcessor` actor receives `FileEvent` messages and performs actions
 * based on the type of event (e.g., CREATED, MODIFIED, DELETED).
 * Its key responsibilities include:
 * - Receiving `FileEvent`s.
 * - Extracting metadata for created or modified files (e.g., size, hash) using
 *   `qb::io::system::file` and standard filesystem operations.
 * - Maintaining a map (`_tracked_files`) of known files and their metadata to detect
 *   actual content changes versus mere timestamp updates for MODIFIED events.
 * - Handling file deletion events by removing files from its tracking map.
 * - Allowing configuration changes via `SetProcessingConfigRequest` (e.g., whether to
 *   process hidden files).
 * - Reporting its processing statistics via `ProcessingStats` upon receiving a
 *   `GetProcessingStatsRequest` (though in this example, it just logs to cout).
 *
 * QB Features Demonstrated:
 * - `qb::Actor`: For encapsulating file processing logic.
 * - `qb::Event`: Base for `FileEvent` and other control/stats events.
 * - Event Handling: `onInit()`, `on(FileEvent&)`, `on(SetProcessingConfigRequest&)`, etc.
 * - State Management: Maintaining `_tracked_files` and `_stats`.
 * - `qb::io::system::file`: Used for reading file content to calculate a hash (demonstrating
 *   integration of synchronous file I/O within an actor context, typically for short operations
 *   or operations that would be further offloaded if very long).
 * - `qb::KillEvent` handling for graceful shutdown.
 */

#pragma once

#include <qb/actor.h>
#include <string>
#include <unordered_map>
#include <filesystem>
#include "events.h"

namespace file_monitor {

namespace fs = std::filesystem;

/**
 * @brief Actor for processing file events
 * 
 * This actor receives file events from the DirectoryWatcher
 * and performs appropriate processing on them
 */
class FileProcessor : public qb::Actor {
private:
    // Track files by path
    std::unordered_map<std::string, FileMetadata> _tracked_files;
    
    // Path where processor is monitoring
    std::string _base_path;
    
    // Statistics tracking
    ProcessingStats _stats;
    
    // Configuration
    bool _process_hidden_files = false;
    
public:
    /**
     * @brief Constructor
     */
    explicit FileProcessor(const std::string& base_path);
    
    /**
     * @brief Initialize the actor
     */
    bool onInit() override;
    
    /**
     * @brief Event handlers
     */
    void on(FileEvent& event);
    void on(SetProcessingConfigRequest& request);
    void on(GetProcessingStatsRequest& request);
    void on(qb::KillEvent&);
    
private:
    /**
     * @brief Processing methods for different file events
     */
    void processFileCreated(const std::string& path);
    void processFileModified(const std::string& path);
    void processFileDeleted(const std::string& path);
    
    /**
     * @brief Check if a file should be processed
     * 
     * Determines if a file should be processed based on its extension,
     * attributes, and configuration settings
     */
    bool shouldProcessFile(const std::string& path);
    
    /**
     * @brief Extract file metadata
     */
    FileMetadata extractMetadata(const std::string& path);
    
    /**
     * @brief Update statistics
     */
    void updateStats(FileEventType event_type);
};

} // namespace file_monitor 