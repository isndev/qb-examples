/**
 * @file examples/core_io/file_monitor/watcher.h
 * @example File Monitoring System - Directory Watcher Actor Definition
 * @brief Defines the `DirectoryWatcher` actor and its helper `DirectoryMonitor` class
 *        for monitoring file system changes.
 *
 * @details
 * This header declares two main classes:
 * 1.  `DirectoryMonitor`:
 *     -   A helper class that inherits from `qb::io::async::directory_watcher<DirectoryMonitor>`.
 *     -   It is responsible for directly interfacing with QB-IO's low-level file watching mechanism.
 *     -   Its `on(qb::io::async::event::file const& event)` method is called by QB-IO when
 *         a file system event occurs in the watched directory.
 *     -   It then invokes a C++ `std::function` callback (provided by `DirectoryWatcher` actor)
 *         to notify about the detected `FileEventType`.
 *     -   Manages its own lifecycle with `startWatching()` and `stopWatching()`.
 * 2.  `DirectoryWatcher` (Actor):
 *     -   An `qb::Actor` that manages one or more `DirectoryMonitor` instances.
 *     -   Receives `WatchDirectoryRequest` to start monitoring a new path. For each such
 *         request, it may create a `DirectoryMonitor` if not already watching that path or a parent.
 *     -   Handles recursive watching by potentially creating `DirectoryMonitor`s for subdirectories.
 *     -   Stores subscriber `ActorId`s for each watched path and, upon receiving a notification
 *         from a `DirectoryMonitor`, creates a `FileEvent` and `push`es it to all relevant subscribers.
 *     -   Receives `UnwatchDirectoryRequest` to stop monitoring and clean up resources.
 *     -   Manages statistics about watched directories and events (`MonitoringStats`).
 *
 * QB Features Demonstrated:
 * - `qb::Actor`: For the main `DirectoryWatcher` logic.
 * - `qb::io::async::directory_watcher<T>`: CRTP base class for asynchronous directory monitoring.
 * - `on(qb::io::async::event::file const& event)`: Handler for low-level file events.
 * - `qb::Event`: Base for `WatchDirectoryRequest`, `FileEvent`, etc.
 * - Inter-Actor Communication: Receiving requests and sending event notifications and responses.
 * - Asynchronous Callbacks: `qb::io::async::callback` used internally by `DirectoryWatcher` to
 *   set up watches asynchronously after a request.
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