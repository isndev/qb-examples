/**
 * @file examples/core_io/file_processor/file_manager.h
 * @example Distributed File Processor - File Manager Actor Definition
 * @brief Defines the `FileManager` actor, which acts as a dispatcher for file
 *        processing requests to a pool of `FileWorker` actors.
 *
 * @details
 * The `FileManager` actor is central to the distributed file processing system.
 * Its primary roles are:
 * - **Request Reception**: Receives `ReadFileRequest` and `WriteFileRequest` events from `ClientActor`(s).
 * - **Request Queuing**: If no `FileWorker` actors are immediately available, it queues incoming
 *   requests (`_read_requests`, `_write_requests`). Read requests are implicitly prioritized.
 * - **Worker Management**: Maintains a set of available `FileWorker` IDs (`_available_workers`).
 *   It listens for `WorkerAvailable` events sent by `FileWorker`s when they complete a task.
 * - **Task Dispatching**: When a request arrives and a worker is available, or when a worker
 *   becomes available and there are queued requests, it assigns the task to a worker by
 *   `push`ing the original request event (e.g., `ReadFileRequest`) to the chosen worker's `ActorId`.
 * - **Response Forwarding**: Receives `ReadFileResponse` and `WriteFileResponse` events from
 *   `FileWorker`s (after they complete an operation) and forwards these responses to the
 *   original `requestor` (the `ClientActor`) identified in the response event.
 * - **Request ID Management**: Assigns unique request IDs if not provided by the client.
 * - Handles `qb::KillEvent` for graceful shutdown.
 *
 * This actor demonstrates a manager-worker pattern, request queuing, and dynamic task assignment.
 *
 * QB Features Demonstrated:
 * - `qb::Actor`: For orchestrating file processing tasks.
 * - Event Handling: `onInit()`, `on(ReadFileRequest&)`, `on(WriteFileRequest&)`,
 *   `on(WorkerAvailable&)`, `on(ReadFileResponse&)`, `on(WriteFileResponse&)`, `on(qb::KillEvent&)`.
 * - Inter-Actor Communication: Receiving requests, dispatching tasks to workers, and forwarding responses.
 * - State Management: Maintaining queues of requests (`std::queue`) and a set of available workers (`std::unordered_set`).
 * - Dynamic Task Assignment to a worker pool.
 */

#pragma once

#include <qb/actor.h>
#include <iostream>
#include <queue>
#include <unordered_set>
#include <atomic>
#include "messages.h"

namespace file_processor {

/**
 * @brief Actor that manages file read/write requests
 * 
 * FileManager is responsible for receiving file processing requests
 * and distributing them to available workers. It tracks worker status and
 * maintains a queue for requests when no workers are available.
 */
class FileManager : public qb::Actor {
private:
    // IDs of available workers
    std::unordered_set<qb::ActorId> _available_workers;
    
    // Request queues
    std::queue<ReadFileRequest> _read_requests;
    std::queue<WriteFileRequest> _write_requests;
    
    // Counter for request IDs
    std::atomic<uint32_t> _request_counter{0};
    
public:
    /**
     * @brief Default constructor
     */
    FileManager() {
        // Register for event types
        registerEvent<ReadFileRequest>(*this);
        registerEvent<WriteFileRequest>(*this);
        registerEvent<WorkerAvailable>(*this);
        registerEvent<ReadFileResponse>(*this);
        registerEvent<WriteFileResponse>(*this);
        registerEvent<qb::KillEvent>(*this);
    }
    
    /**
     * @brief Actor initialization method
     */
    bool onInit() override {
        qb::io::cout() << "FileManager initialized with ID " << id() << " on core " << id().index() << std::endl;
        return true;
    }
    
    /**
     * @brief Processes a read request
     */
    void on(ReadFileRequest& request) {
        // Add the requester ID to the request if not already present
        if (request.requestor == qb::ActorId{}) {
            request.requestor = request.getSource();
        }
        
        // Assign a request ID if not provided
        if (request.request_id == 0) {
            request.request_id = ++_request_counter;
        }
        
        qb::io::cout() << "FileManager received a read request for "
                  << request.filepath.c_str() << " (ID: " << request.request_id << ")" << std::endl;
        
        // If a worker is available, assign the task
        if (!_available_workers.empty()) {
            qb::ActorId worker_id = *_available_workers.begin();
            _available_workers.erase(worker_id);
            
            qb::io::cout() << "FileManager assigns the read task to worker " << worker_id << std::endl;
            push<ReadFileRequest>(worker_id, request.filepath.c_str(), request.requestor, request.request_id);
        } else {
            // Otherwise, queue the request
            qb::io::cout() << "FileManager queues the read request" << std::endl;
            _read_requests.push(request);
        }
    }
    
    /**
     * @brief Processes a write request
     */
    void on(WriteFileRequest& request) {
        // Add the requester ID to the request if not already present
        if (request.requestor == qb::ActorId{}) {
            request.requestor = request.getSource();
        }
        
        // Assign a request ID if not provided
        if (request.request_id == 0) {
            request.request_id = ++_request_counter;
        }
        
        qb::io::cout() << "FileManager received a write request for "
                  << request.filepath.c_str() << " (ID: " << request.request_id << ")" << std::endl;
        
        // If a worker is available, assign the task
        if (!_available_workers.empty()) {
            qb::ActorId worker_id = *_available_workers.begin();
            _available_workers.erase(worker_id);
            
            qb::io::cout() << "FileManager assigns the write task to worker " << worker_id << std::endl;
            push<WriteFileRequest>(
                worker_id, 
                request.filepath.c_str(), 
                request.data, 
                request.requestor, 
                request.request_id
            );
        } else {
            // Otherwise, queue the request
            qb::io::cout() << "FileManager queues the write request" << std::endl;
            _write_requests.push(request);
        }
    }
    
    /**
     * @brief Handles notification from an available worker
     */
    void on(WorkerAvailable& msg) {
        qb::io::cout() << "FileManager received an availability notification from worker "
                  << msg.worker_id << std::endl;
        
        // Try to assign a task to the available worker
        if (!_read_requests.empty()) {
            // Priority to read operations
            ReadFileRequest request = _read_requests.front();
            _read_requests.pop();
            
            qb::io::cout() << "FileManager assigns a queued read task to worker "
                      << msg.worker_id << std::endl;
                      
            push<ReadFileRequest>(
                msg.worker_id,
                request.filepath.c_str(),
                request.requestor,
                request.request_id
            );
        } else if (!_write_requests.empty()) {
            // Then write operations
            WriteFileRequest request = _write_requests.front();
            _write_requests.pop();
            
            qb::io::cout() << "FileManager assigns a queued write task to worker "
                      << msg.worker_id << std::endl;
                      
            push<WriteFileRequest>(
                msg.worker_id,
                request.filepath.c_str(),
                request.data,
                request.requestor,
                request.request_id
            );
        } else {
            // No pending tasks, keep the worker available
            _available_workers.insert(msg.worker_id);
        }
    }
    
    /**
     * @brief Forwards a read response
     */
    void on(ReadFileResponse& response) {
        qb::io::cout() << "FileManager received a read response for "
                  << response.filepath.c_str() << " (ID: " << response.request_id << ")" << std::endl;
        
        // Forward the response to the requesting actor
        push<ReadFileResponse>(
            response.getSource(), 
            response.filepath.c_str(), 
            response.data, 
            response.success, 
            response.error_msg.c_str(), 
            response.request_id
        );
    }
    
    /**
     * @brief Forwards a write response
     */
    void on(WriteFileResponse& response) {
        qb::io::cout() << "FileManager received a write response for "
                  << response.filepath.c_str() << " (ID: " << response.request_id << ")" << std::endl;
        
        // Forward the response to the requesting actor
        push<WriteFileResponse>(
            response.getSource(), 
            response.filepath.c_str(), 
            response.bytes_written, 
            response.success, 
            response.error_msg.c_str(), 
            response.request_id
        );
    }
    
    /**
     * @brief Stops the FileManager
     */
    void on(qb::KillEvent&) {
        qb::io::cout() << "FileManager shutting down" << std::endl;
        kill();
    }
};

} // namespace file_processor 