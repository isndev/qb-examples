/**
 * @file messages.h
 * @brief Definition of shared messages/events between actors
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