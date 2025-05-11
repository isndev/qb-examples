/**
 * @file examples/core_io/file_processor/messages.h
 * @example Distributed File Processor - Shared Event/Message Definitions
 * @brief Defines custom `qb::Event` types used for communication between actors
 *        in the distributed file processing system example.
 *
 * @details
 * This header declares events for requesting file operations (read/write),
 * responding to those requests, and worker status notifications.
 *
 * Event Types:
 * - `ReadFileRequest`: A `qb::Event` sent to `FileManager` to request reading a file.
 *   Contains file path, requestor's `ActorId`, and a unique request ID.
 * - `ReadFileResponse`: A `qb::Event` sent back from `FileManager` (forwarded from `FileWorker`)
 *   with the content of the read file (or error). Contains file path, data (`std::shared_ptr<std::vector<char>>`),
 *   success status, error message, and the original request ID.
 * - `WriteFileRequest`: A `qb::Event` sent to `FileManager` to request writing data to a file.
 *   Contains file path, data to write (`std::shared_ptr<std::vector<char>>`), requestor's `ActorId`, and request ID.
 * - `WriteFileResponse`: A `qb::Event` sent back from `FileManager` (forwarded from `FileWorker`)
 *   indicating the result of the write operation. Contains file path, bytes written,
 *   success status, error message, and request ID.
 * - `WorkerAvailable`: A `qb::Event` sent by a `FileWorker` to the `FileManager` to indicate
 *   it is ready to process a new task.
 *
 * QB Features Demonstrated:
 * - Custom `qb::Event` Creation for application-specific messaging.
 * - Data Encapsulation: Events carry all necessary data for the operation/response.
 * - Use of `qb::string<N>` for fixed-size string fields in events.
 * - `qb::ActorId` for routing responses back to the original requestor.
 * - `std::shared_ptr<std::vector<char>>` for efficient handling of potentially large file data.
 */

#pragma once

#include <qb/actor.h>
#include <qb/event.h>
#include <qb/string.h>
#include <string>
#include <vector>
#include <memory>

namespace file_processor {

/**
 * @brief File read request
 */
struct ReadFileRequest : public qb::Event {
    qb::string<256> filepath;   // Path of the file to read
    qb::ActorId requestor;      // Actor that made the request
    uint32_t request_id;        // Unique request ID

    ReadFileRequest(const char* path, qb::ActorId req_id, uint32_t id)
        : filepath(path), requestor(req_id), request_id(id) {}
};

/**
 * @brief Response to a read request
 */
struct ReadFileResponse : public qb::Event {
    qb::string<256> filepath;   // Path of the file that was read
    std::shared_ptr<std::vector<char>> data; // File content
    bool success;               // Indicates if the read was successful
    qb::string<256> error_msg;  // Error message if any
    uint32_t request_id;        // Corresponding request ID

    ReadFileResponse(const char* path, std::shared_ptr<std::vector<char>> content, 
                    bool ok, const char* error, uint32_t id)
        : filepath(path), data(content), success(ok), error_msg(error), request_id(id) {}
};

/**
 * @brief File write request
 */
struct WriteFileRequest : public qb::Event {
    qb::string<256> filepath;   // Path of the file to write
    std::shared_ptr<std::vector<char>> data; // Content to write
    qb::ActorId requestor;      // Actor that made the request
    uint32_t request_id;        // Unique request ID

    WriteFileRequest(const char* path, std::shared_ptr<std::vector<char>> content, 
                    qb::ActorId req_id, uint32_t id)
        : filepath(path), data(content), requestor(req_id), request_id(id) {}
};

/**
 * @brief Response to a write request
 */
struct WriteFileResponse : public qb::Event {
    qb::string<256> filepath;   // Path of the file that was written
    size_t bytes_written;       // Number of bytes written
    bool success;               // Indicates if the write was successful
    qb::string<256> error_msg;  // Error message if any
    uint32_t request_id;        // Corresponding request ID

    WriteFileResponse(const char* path, size_t bytes, bool ok, const char* error, uint32_t id)
        : filepath(path), bytes_written(bytes), success(ok), error_msg(error), request_id(id) {}
};

/**
 * @brief Worker availability message
 */
struct WorkerAvailable : public qb::Event {
    qb::ActorId worker_id;      // ID of the available worker actor

    explicit WorkerAvailable(qb::ActorId id) : worker_id(id) {}
};

} // namespace file_processor 