/**
 * @file watcher.cpp
 * @brief Implementation of classes for file monitoring
 */

#include "watcher.h"
#include <iostream>

namespace file_monitor {

//==============================================================================
// DirectoryMonitor implementation
//==============================================================================

DirectoryMonitor::DirectoryMonitor(FileEventCallback callback) 
    : _callback(callback) {
}

void DirectoryMonitor::on(qb::io::async::event::file const& event) {
    std::string file_path = event.path;
    
    // Determine the type of event
    FileEventType event_type = FileEventType::MODIFIED;
    
    // Check attributes
    if (event.attr.st_mode != event.prev.st_mode ||
        event.attr.st_uid != event.prev.st_uid ||
        event.attr.st_gid != event.prev.st_gid) {
        event_type = FileEventType::ATTRIBUTES_CHANGED;
    }
    // Check if file was deleted
    else if (!fs::exists(file_path)) {
        event_type = FileEventType::DELETED;
    }
    // Check if file was created or is new to us
    else if (_last_mtimes.find(file_path) == _last_mtimes.end() ||
             event.attr.st_nlink > event.prev.st_nlink) {
        event_type = FileEventType::CREATED;
    }
    // Otherwise it's a modification
    else if (event.attr.st_mtime != event.prev.st_mtime) {
        event_type = FileEventType::MODIFIED;
    }
    
    // Store the last modification time
    if (fs::exists(file_path)) {
        _last_mtimes[file_path] = event.attr.st_mtime;
    } else {
        _last_mtimes.erase(file_path);
    }
    
    // Invoke the callback
    if (_callback) {
        _callback(file_path, event_type);
    }
}

void DirectoryMonitor::startWatching(const std::string& path, double interval) {
    start(path, interval);
}

void DirectoryMonitor::stopWatching() {
    disconnect();
}

//==============================================================================
// DirectoryWatcher implementation
//==============================================================================

DirectoryWatcher::DirectoryWatcher() {
    // Register for message types
    registerEvent<WatchDirectoryRequest>(*this);
    registerEvent<UnwatchDirectoryRequest>(*this);
    registerEvent<qb::KillEvent>(*this);
}

bool DirectoryWatcher::onInit() {
    std::cout << "DirectoryWatcher initialized on core " << id().index() << std::endl;
    return true;
}

void DirectoryWatcher::on(WatchDirectoryRequest& request) {
    std::cout << "DirectoryWatcher received request to watch: " << request.path << std::endl;
    
    // Normalize the path
    std::string normalized_path = fs::absolute(request.path).string();
    
    // Check if the directory exists
    if (!fs::exists(normalized_path) || !fs::is_directory(normalized_path)) {
        // Directory doesn't exist or is not a directory
        push<WatchDirectoryResponse>(
            request.requestor, 
            normalized_path, 
            false, 
            "Path does not exist or is not a directory"
        );
        return;
    }
    
    // Create or get the watched directory object
    auto watch = getOrCreateWatch(normalized_path, request.recursive);
    
    // Add the requestor to subscribers if not already there
    if (std::find(watch->subscribers.begin(), watch->subscribers.end(), request.requestor) 
        == watch->subscribers.end()) {
        watch->subscribers.push_back(request.requestor);
    }
    
    // Scan the directory and set up watchers asynchronously
    qb::io::async::callback([this, normalized_path, watch, request]() {
        bool success = setupDirectoryWatch(normalized_path, watch, request.recursive);
        
        // Send response to requestor
        push<WatchDirectoryResponse>(
            request.requestor, 
            normalized_path, 
            success, 
            success ? "" : "Failed to set up directory watch"
        );
        
        // Update statistics
        _stats.directories_watched = _watched_directories.size();
        
        int total_files = 0;
        for (const auto& pair : _watched_directories) {
            total_files += countWatchedFiles(pair.second);
        }
        _stats.files_monitored = total_files;
    });
}

void DirectoryWatcher::on(UnwatchDirectoryRequest& request) {
    std::cout << "DirectoryWatcher received request to unwatch: " << request.path << std::endl;
    
    // Normalize the path
    std::string normalized_path = fs::absolute(request.path).string();
    
    // Find the watch
    auto it = _watched_directories.find(normalized_path);
    if (it != _watched_directories.end()) {
        // Remove the requestor from subscribers
        auto& subscribers = it->second->subscribers;
        subscribers.erase(
            std::remove(subscribers.begin(), subscribers.end(), request.requestor),
            subscribers.end()
        );
        
        // If no subscribers left, remove the watch
        if (subscribers.empty()) {
            // Stop all watchers
            if (it->second->watcher) {
                it->second->watcher->stopWatching();
            }
            _watched_directories.erase(it);
        }
        
        // Update statistics
        _stats.directories_watched = _watched_directories.size();
        
        int total_files = 0;
        for (const auto& pair : _watched_directories) {
            total_files += countWatchedFiles(pair.second);
        }
        _stats.files_monitored = total_files;
    }
}

void DirectoryWatcher::on(qb::KillEvent&) {
    std::cout << "DirectoryWatcher shutting down" << std::endl;
    
    // Remove all watches
    for (auto& pair : _watched_directories) {
        if (pair.second->watcher) {
            pair.second->watcher->stopWatching();
        }
    }
    _watched_directories.clear();
    
    kill();
}

std::shared_ptr<DirectoryWatcher::WatchInfo> 
DirectoryWatcher::getOrCreateWatch(const std::string& path, bool recursive) {
    auto it = _watched_directories.find(path);
    if (it != _watched_directories.end()) {
        // Update recursion flag if needed
        if (recursive && !it->second->recursive) {
            it->second->recursive = true;
        }
        return it->second;
    }
    
    // Create a new watch
    auto watch = std::make_shared<WatchInfo>();
    watch->path = path;
    watch->recursive = recursive;
    _watched_directories[path] = watch;
    
    return watch;
}

bool DirectoryWatcher::setupDirectoryWatch(const std::string& path, 
                                           std::shared_ptr<WatchInfo> watch,
                                           bool recursive) {
    try {
        // Create the DirectoryMonitor with a callback
        watch->watcher = std::make_unique<DirectoryMonitor>(
            [this, path](const std::string& file_path, FileEventType event_type) {
                // Create and publish the event
                publishFileEvent(file_path, event_type);
                
                // Update statistics
                switch (event_type) {
                    case FileEventType::CREATED:
                        _stats.created_events++;
                        break;
                    case FileEventType::MODIFIED:
                        _stats.modified_events++;
                        break;
                    case FileEventType::DELETED:
                        _stats.deleted_events++;
                        break;
                    case FileEventType::ATTRIBUTES_CHANGED:
                        _stats.attribute_events++;
                        break;
                }
            }
        );
        
        // Start watching the directory
        watch->watcher->startWatching(path, 0.5); // Check every 500ms
        
        // Process subdirectories if recursive
        if (recursive) {
            for (const auto& entry : fs::directory_iterator(path)) {
                if (entry.is_directory()) {
                    std::string subdir_path = entry.path().string();
                    
                    // Create a subwatch
                    auto subwatch = std::make_shared<WatchInfo>();
                    subwatch->path = subdir_path;
                    subwatch->recursive = true;
                    subwatch->subscribers = watch->subscribers; // Inherit subscribers
                    watch->subdirectories[subdir_path] = subwatch;
                    
                    // Set up the recursive watch
                    setupDirectoryWatch(subdir_path, subwatch, true);
                }
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error setting up directory watch: " << e.what() << std::endl;
        return false;
    }
}

void DirectoryWatcher::publishFileEvent(const std::string& file_path, FileEventType event_type) {
    // Find which directory watch contains this file
    std::shared_ptr<WatchInfo> watch;
    std::string watch_dir;
    
    // Find the deepest directory that contains this file
    for (const auto& pair : _watched_directories) {
        if (file_path.find(pair.first) == 0) { // path starts with dir
            if (watch_dir.empty() || pair.first.length() > watch_dir.length()) {
                watch_dir = pair.first;
                watch = pair.second;
            }
        }
    }
    
    if (watch) {
        // Log the event
        std::cout << "File event: " << eventTypeToString(event_type) 
                  << " - " << file_path << std::endl;
        
        // Publish to all subscribers
        for (const auto& subscriber : watch->subscribers) {
            push<FileEvent>(subscriber, file_path, event_type);
        }
    }
}

int DirectoryWatcher::countWatchedFiles(std::shared_ptr<WatchInfo> watch) {
    int count = 0;
    
    try {
        // Count files in the directory (excluding subdirectories)
        for (const auto& entry : fs::directory_iterator(watch->path)) {
            if (!entry.is_directory()) {
                count++;
            }
        }
        
        // Count files in subdirectories
        for (const auto& pair : watch->subdirectories) {
            count += countWatchedFiles(pair.second);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error counting files: " << e.what() << std::endl;
    }
    
    return count;
}

} // namespace file_monitor 