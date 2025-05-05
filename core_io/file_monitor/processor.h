/**
 * @file processor.h
 * @brief Classes for processing file events
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