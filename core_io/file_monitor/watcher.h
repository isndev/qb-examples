/**
 * @file watcher.h
 * @brief Classes for directory and file monitoring
 */

#pragma once

#include <qb/actor.h>
#include <qb/io/async.h>
#include <map>
#include <memory>
#include <vector>
#include <functional>
#include <filesystem>
#include "events.h"

namespace file_monitor {

namespace fs = std::filesystem;

// Forward declaration
class DirectoryMonitor;
using FileEventCallback = std::function<void(const std::string&, FileEventType)>;

/**
 * @brief Custom watcher for monitoring files
 * 
 * Implemented using qb::io::async::directory_watcher
 * following the CRTP pattern (Curiously Recurring Template Pattern)
 */
class DirectoryMonitor : public qb::io::async::directory_watcher<DirectoryMonitor> {
private:
    FileEventCallback _callback;
    std::map<std::string, time_t> _last_mtimes;
    
public:
    /**
     * @brief Constructor
     */
    explicit DirectoryMonitor(FileEventCallback callback);
    
    /**
     * @brief Process a file event
     */
    void on(qb::io::async::event::file const& event);
    
    /**
     * @brief Start watching a directory
     */
    void startWatching(const std::string& path, double interval = 0.5);
    
    /**
     * @brief Stop watching
     */
    void stopWatching();
};

/**
 * @brief DirectoryWatcher actor that uses qb::io::async::directory_watcher for monitoring
 * 
 * This actor monitors directories and notifies subscribers about file
 * changes in real-time
 */
class DirectoryWatcher : public qb::Actor {
private:
    // Structure to track watched directory
    struct WatchInfo {
        std::string path;
        bool recursive;
        std::vector<qb::ActorId> subscribers;
        std::unique_ptr<DirectoryMonitor> watcher;
        std::map<std::string, std::shared_ptr<WatchInfo>> subdirectories;
    };
    
    // Root watched directories
    std::map<std::string, std::shared_ptr<WatchInfo>> _watched_directories;
    
    // Statistics tracking
    MonitoringStats _stats;
    
public:
    /**
     * @brief Constructor
     */
    DirectoryWatcher();
    
    /**
     * @brief Initialize the actor
     */
    bool onInit() override;
    
    /**
     * @brief Event handlers
     */
    void on(WatchDirectoryRequest& request);
    void on(UnwatchDirectoryRequest& request);
    void on(qb::KillEvent&);
    
private:
    /**
     * @brief Utility functions
     */
    std::shared_ptr<WatchInfo> getOrCreateWatch(const std::string& path, bool recursive);
    bool setupDirectoryWatch(const std::string& path, std::shared_ptr<WatchInfo> watch, bool recursive);
    void publishFileEvent(const std::string& file_path, FileEventType event_type);
    int countWatchedFiles(std::shared_ptr<WatchInfo> watch);
};

} // namespace file_monitor 