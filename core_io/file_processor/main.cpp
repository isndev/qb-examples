/**
 * @file main.cpp
 * @brief Main program for the file_processor example
 * 
 * This program demonstrates the use of qb::Actor actors combined
 * with qb::io asynchronous I/O to create a distributed file
 * processing system.
 */

#include <qb/main.h>
#include <qb/actor.h>
#include <qb/io/async.h>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <filesystem>

#include "file_manager.h"
#include "file_worker.h"
#include "messages.h"

// To simplify namespaces
namespace fs = std::filesystem;
using namespace file_processor;

/**
 * @brief Client actor that sends test requests
 */
class ClientActor : public qb::Actor {
private:
    qb::ActorId _manager_id;
    std::string _test_directory;
    uint32_t _next_request_id = 1;
    uint32_t _pending_requests = 0;
    
public:
    ClientActor(qb::ActorId manager_id, const std::string& test_dir)
        : _manager_id(manager_id), _test_directory(test_dir) {
        // Register for response types
        registerEvent<ReadFileResponse>(*this);
        registerEvent<WriteFileResponse>(*this);
        registerEvent<qb::KillEvent>(*this);
    }
    
    bool onInit() override {
        qb::io::cout() << "ClientActor initialized with ID " << id() << " on core " << id().index() << std::endl;
        
        // Ensure the test directory exists
        if (!fs::exists(_test_directory)) {
            fs::create_directories(_test_directory);
        }
        
        // Start tests after a short delay
        qb::io::async::callback([this]() {
            startTests();
        }, 0.5);
        
        return true;
    }
    
    void on(ReadFileResponse& response) {
        qb::io::cout() << "ClientActor received a read response for "
                  << response.filepath.c_str() << " (ID: " << response.request_id << ")" << std::endl;
        
        if (response.success) {
            qb::io::cout() << "File content (" << response.data->size() << " bytes): ";
            
            // Display the first few characters of the file
            const size_t max_display = 50;  // Limit display
            size_t display_size = std::min(response.data->size(), max_display);
            
            std::string content(response.data->begin(), response.data->begin() + display_size);
            qb::io::cout() << content;
            
            if (response.data->size() > max_display) {
                qb::io::cout() << "... [plus " << (response.data->size() - max_display) << " bytes]";
            }
            qb::io::cout() << std::endl;
        } else {
            qb::io::cout() << "Error: " << response.error_msg.c_str() << std::endl;
        }
        
        _pending_requests--;
        checkCompletion();
    }
    
    void on(WriteFileResponse& response) {
        qb::io::cout() << "ClientActor received a write response for "
                  << response.filepath.c_str() << " (ID: " << response.request_id << ")" << std::endl;
        
        if (response.success) {
            qb::io::cout() << "Write successful: " << response.bytes_written << " bytes written" << std::endl;
            
            // Request to read the file that was just written
            requestReadFile(response.filepath.c_str());
        } else {
            qb::io::cout() << "Write error: " << response.error_msg.c_str() << std::endl;
        }
        
        _pending_requests--;
        checkCompletion();
    }
    
    void on(qb::KillEvent&) {
        qb::io::cout() << "ClientActor shutting down" << std::endl;
        kill();
    }
    
private:
    void startTests() {
        qb::io::cout() << "\n=== Starting file operation tests ===\n" << std::endl;
        
        // Create some test files
        for (int i = 1; i <= 5; ++i) {
            std::string filename = _test_directory + "/test_file_" + std::to_string(i) + ".txt";
            std::string content = "This is the content of test file " + std::to_string(i) + ".\n";
            content += "Created by ClientActor to demonstrate distributed file processing.\n";
            content += "Random line " + std::to_string(rand() % 1000) + " to make the content unique.\n";
            
            // Add extra content for larger files
            for (int j = 0; j < i; ++j) {
                content += "Extra line " + std::to_string(j) + " to increase file size.\n";
            }
            
            requestWriteFile(filename, content);
        }
    }
    
    void requestWriteFile(const std::string& filepath, const std::string& content) {
        qb::io::cout() << "ClientActor requesting file write: " << filepath << std::endl;
        
        // Create a vector with the content
        auto data = std::make_shared<std::vector<char>>(content.begin(), content.end());
        
        // Send the request to the manager
        uint32_t request_id = _next_request_id++;
        push<WriteFileRequest>(_manager_id, filepath.c_str(), data, id(), request_id);
        
        _pending_requests++;
    }
    
    void requestReadFile(const std::string& filepath) {
        qb::io::cout() << "ClientActor requesting file read: " << filepath << std::endl;
        
        // Send the request to the manager
        uint32_t request_id = _next_request_id++;
        push<ReadFileRequest>(_manager_id, filepath.c_str(), id(), request_id);
        
        _pending_requests++;
    }
    
    void checkCompletion() {
        // If all requests have been processed, wait a bit then stop
        if (_pending_requests == 0) {
            qb::io::cout() << "\n=== All tests completed ===\n" << std::endl;
            
            // Wait a bit then stop the system - use built-in KillEvent
            qb::io::async::callback([this]() {
                // Broadcast the Kill event
                broadcast<qb::KillEvent>();
            }, 1.0);
        }
    }
};

int main(int argc, char** argv) {
    qb::io::cout() << "=== QB Core/IO Example: Distributed File Processing ===\n" << std::endl;
    
    // Define the test file directory
    std::string test_dir = "./test_files";
    
    try {
        // Initialize the qb actor system
        qb::Main engine;
        
        // Create the file manager on core 0
        auto manager_id = engine.addActor<FileManager>(0);
        
        // Create multiple workers on different cores
        const int num_workers = 4;
        std::vector<qb::ActorId> worker_ids;
        
        for (int i = 0; i < num_workers; ++i) {
            // Distribute workers across cores 1, 2, 3, ...
            int core_id = 1 + (i % 3);  // Use cores 1, 2, 3
            
            auto worker_id = engine.addActor<FileWorker>(core_id, manager_id);
            worker_ids.push_back(worker_id);
            
            qb::io::cout() << "Worker " << i+1 << " created on core " << core_id << std::endl;
        }
        
        // Create the client on core 0
        auto client_id = engine.addActor<ClientActor>(0, manager_id, test_dir);
        
        // Start the system
        engine.start();
        
        // Wait for all actors to terminate - qb::Main automatically handles signals
        engine.join();
        qb::io::cout() << "System shut down correctly" << std::endl;
        
    } catch (const std::exception& e) {
        qb::io::cerr() << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 