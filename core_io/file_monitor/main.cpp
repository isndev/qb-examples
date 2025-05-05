/**
 * @file main.cpp
 * @brief File monitoring system using qb-io asynchronous operations
 * 
 * This program demonstrates the use of qb::Actor actors combined with qb::io 
 * asynchronous I/O to create a real-time file monitoring system.
 * 
 * Features:
 * - Monitors directories for file changes in real-time
 * - Uses qb::io::async::directory_watcher for file watching
 * - Processes file changes through a distributed actor system
 * - Demonstrates proper qb-io async patterns
 */

#include <qb/main.h>
#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/system/file.h>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <filesystem>
#include <functional>
#include <atomic>

// Include our separated header files
#include "events.h"
#include "watcher.h"
#include "processor.h"

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════
// CLIENT ACTOR
// ═══════════════════════════════════════════════════════════════════

/**
 * @brief ClientActor that creates and monitors test files
 */
class ClientActor : public qb::Actor {
private:
    qb::ActorId _watcher_id;
    std::string _test_directory;
    int _test_duration_seconds;
    bool _is_running = false;
    
public:
    /**
     * @brief Constructor
     */
    ClientActor(qb::ActorId watcher_id, const std::string& test_dir, int duration = 30)
        : _watcher_id(watcher_id), _test_directory(test_dir), _test_duration_seconds(duration) {
        
        // Register for event types
        registerEvent<file_monitor::FileEvent>(*this);
        registerEvent<file_monitor::WatchDirectoryResponse>(*this);
        registerEvent<qb::KillEvent>(*this);
    }
    
    /**
     * @brief Initialize the actor
     */
    bool onInit() override {
        std::cout << "ClientActor initialized on core " << id().index() << std::endl;
        
        // Ensure the test directory exists
        if (!fs::exists(_test_directory)) {
            fs::create_directories(_test_directory);
        }
        
        // Start monitoring after a short delay
        qb::io::async::callback([this]() {
            startMonitoring();
        }, 0.5);
        
        return true;
    }
    
    /**
     * @brief Handle file events
     */
    void on(file_monitor::FileEvent& event) {
        std::cout << "Client received " << file_monitor::eventTypeToString(event.type) 
                 << " event for: " << event.path << std::endl;
    }
    
    /**
     * @brief Handle watch directory response
     */
    void on(file_monitor::WatchDirectoryResponse& response) {
        if (response.success) {
            std::cout << "Successfully set up watch for: " << response.path << std::endl;
            
            // Start file tests
            startFileTests();
        } else {
            std::cerr << "Failed to set up watch: " << response.error_message << std::endl;
        }
    }
    
    /**
     * @brief Handle kill event
     */
    void on(qb::KillEvent&) {
        std::cout << "ClientActor shutting down" << std::endl;
        
        // Stop running
        _is_running = false;
        
        // Unwatch the directory
        push<file_monitor::UnwatchDirectoryRequest>(_watcher_id, _test_directory, id());
        
        kill();
    }
    
private:
    /**
     * @brief Start monitoring directories
     */
    void startMonitoring() {
        std::cout << "Starting to monitor directory: " << _test_directory << std::endl;
        
        // Request to watch the directory
        push<file_monitor::WatchDirectoryRequest>(_watcher_id, _test_directory, true, id());
    }
    
    /**
     * @brief Start file test operations
     */
    void startFileTests() {
        _is_running = true;
        
        std::cout << "Starting file test operations for " << _test_duration_seconds << " seconds" << std::endl;
        
        // Create initial test files
        for (int i = 1; i <= 5; ++i) {
            createTestFile(i);
        }
        
        // Schedule file modifications
        scheduleRandomModifications();
        
        // Schedule end of test
        qb::io::async::callback([this]() {
            if (_is_running) {
                std::cout << "Test duration completed, shutting down..." << std::endl;
                broadcast<qb::KillEvent>();
            }
        }, _test_duration_seconds);
    }
    
    /**
     * @brief Create a test file
     */
    void createTestFile(int index) {
        std::string filename = _test_directory + "/test_file_" + std::to_string(index) + ".txt";
        
        // Create the file asynchronously
        qb::io::async::callback([this, filename, index]() {
            try {
                qb::io::sys::file file;
                if (file.open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644) >= 0) {
                    std::string content = "This is test file " + std::to_string(index) + "\n";
                    content += "Created at: " + getCurrentTimestamp() + "\n";
                    content += "Random data: " + std::to_string(std::rand()) + "\n";
                    
                    // Write content
                    file.write(content.c_str(), content.size());
                    file.close();
                    
                    std::cout << "Created test file: " << filename << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error creating test file: " << e.what() << std::endl;
            }
        });
    }
    
    /**
     * @brief Modify a test file
     */
    void modifyTestFile(int index) {
        std::string filename = _test_directory + "/test_file_" + std::to_string(index) + ".txt";
        
        // Modify the file asynchronously
        qb::io::async::callback([this, filename, index]() {
            try {
                if (fs::exists(filename)) {
                    qb::io::sys::file file;
                    if (file.open(filename.c_str(), O_WRONLY | O_APPEND, 0644) >= 0) {
                        std::string content = "Modified at: " + getCurrentTimestamp() + "\n";
                        content += "Additional data: " + std::to_string(std::rand()) + "\n";
                        
                        // Append content
                        file.write(content.c_str(), content.size());
                        file.close();
                        
                        std::cout << "Modified test file: " << filename << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Error modifying test file: " << e.what() << std::endl;
            }
        });
    }
    
    /**
     * @brief Delete a test file
     */
    void deleteTestFile(int index) {
        std::string filename = _test_directory + "/test_file_" + std::to_string(index) + ".txt";
        
        // Delete the file asynchronously
        qb::io::async::callback([filename]() {
            try {
                if (fs::exists(filename)) {
                    fs::remove(filename);
                    std::cout << "Deleted test file: " << filename << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error deleting test file: " << e.what() << std::endl;
            }
        });
    }
    
    /**
     * @brief Schedule random file modifications
     */
    void scheduleRandomModifications() {
        if (!_is_running) return;
        
        // Schedule a random file operation
        int operation = std::rand() % 3; // 0=create, 1=modify, 2=delete
        int file_index = std::rand() % 10 + 1; // 1-10
        
        switch (operation) {
            case 0: // Create
                createTestFile(file_index);
                break;
                
            case 1: // Modify
                modifyTestFile(file_index);
                break;
                
            case 2: // Delete
                deleteTestFile(file_index);
                break;
        }
        
        // Schedule the next operation
        double delay = 0.5 + (std::rand() % 1000) / 1000.0; // 0.5-1.5 seconds
        qb::io::async::callback([this]() {
            scheduleRandomModifications();
        }, delay);
    }
    
    /**
     * @brief Get the current timestamp as a string
     */
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time_t);
        
        char buffer[128];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
        
        return buffer;
    }
};

// ═══════════════════════════════════════════════════════════════════
// MAIN PROGRAM
// ═══════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    std::cout << "=== QB Core/IO Example: File Monitoring System ===\n" << std::endl;
    
    // Define the test directory
    std::string test_dir = "./monitor_test_files";
    
    // Duration of the test in seconds (default: 30 seconds)
    int duration = 30;
    
    // Parse command line arguments
    if (argc > 1) {
        test_dir = argv[1];
    }
    
    if (argc > 2) {
        try {
            duration = std::stoi(argv[2]);
        } catch (...) {
            std::cerr << "Invalid duration parameter, using default" << std::endl;
        }
    }
    
    try {
        // Initialize the qb actor system
        qb::Main engine;
        
        // Create the directory watcher on core 0
        auto watcher_id = engine.addActor<file_monitor::DirectoryWatcher>(0);
        
        // Create the file processor on core 1
        auto processor_id = engine.addActor<file_monitor::FileProcessor>(1, test_dir);
        
        // Create the client on core 0
        auto client_id = engine.addActor<ClientActor>(0, watcher_id, test_dir, duration);
        
        // Start the system
        engine.start();
        
        // Wait for all actors to terminate
        engine.join();
        
        std::cout << "System shut down correctly" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 