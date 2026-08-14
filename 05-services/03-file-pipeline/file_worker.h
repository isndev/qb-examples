/**
 * @file examples/05-services/03-file-pipeline/file_worker.h
 * @example Distributed File Processor - File Worker Actor Definition
 * @brief Defines the `FileWorker` actor, which performs the actual asynchronous
 *        file read and write operations.
 *
 * @details
 * The `FileWorker` actor is responsible for executing file I/O tasks delegated by the
 * `FileManager`. Each worker operates independently.
 *
 * Key Responsibilities:
 * - Receives `ReadFileRequest` and `WriteFileRequest` events from the `FileManager`.
 * - Upon receiving a request, it marks itself as busy.
 * - Runs the potentially blocking file operation (read or write using `qb::io::sys::file`)
 *   inside `qb::io::async::callback(fn)`. Read that as a plain call, not as scheduling: the
 *   ONE-argument overload invokes `fn` INLINE, so the I/O does block this worker's core loop.
 *   What keeps the SYSTEM responsive is the worker pool — the blocking work happens on a worker
 *   actor's core, not on the `FileManager`'s.
 * - After the file operation completes (or fails):
 *   - It creates a `ReadFileResponse` or `WriteFileResponse` event containing the result
 *     (data read, bytes written, success status, error message) and the original request ID.
 *   - It `push`es this response event to the `requestor` specified in the original request
 *     (which is typically the `ClientActor` via the `FileManager`).
 *   - It marks itself as not busy and sends a `WorkerAvailable` event to its `_manager_id`
 *     (the `FileManager`) to signal readiness for new tasks.
 * - Handles `qb::KillEvent` for graceful shutdown.
 *
 * QB Features Demonstrated:
 * - `qb::Actor`: For encapsulating file operation logic.
 * - Event Handling: `onInit()`, `on(ReadFileRequest&)`, `on(WriteFileRequest&)`, `on(qb::KillEvent&)`.
 * - `qb::io::async::callback(fn)`, the one-argument overload, and what it actually does: it runs
 *   `fn` INLINE. The distinction from `callback(fn, delay)` matters for lifetime, not just for
 *   timing — see the note at the call site in `on(ReadFileRequest&)`.
 * - `qb::io::sys::file`: For performing synchronous file open, read, write, and close operations.
 * - Inter-Actor Communication: Sending response events (`ReadFileResponse`, `WriteFileResponse`)
 *   and status events (`WorkerAvailable`) using `push<Event>(...)`.
 * - Managing Actor State: `_is_busy` flag.
 */

#pragma once

#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/system/file.h>
#include <iostream>
#include <memory>
#include <chrono>
#include <sys/stat.h>
#include <fcntl.h>
#include "messages.h"

namespace file_processor {

/**
 * @brief Actor that processes file operations asynchronously
 *
 * FileWorker is responsible for executing requested file operations.
 * It uses qb::io::sys::file to perform operations asynchronously,
 * and sends the results to the requester via events.
 */
class FileWorker : public qb::Actor {
private:
    qb::ActorId _manager_id;      // ID of the file manager
    bool        _is_busy = false; // Indicates if this worker is busy

public:
    /**
     * @brief Constructor
     * @param manager_id ID of the manager actor
     */
    explicit FileWorker(qb::ActorId manager_id)
        : _manager_id(manager_id) {
        // Register for the event types handled by this actor
        registerEvent<ReadFileRequest>(*this);
        registerEvent<WriteFileRequest>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    /**
     * @brief Actor initialization method
     */
    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "FileWorker " << id() << " initialized on core " << id().index() << std::endl;

        // Signal that this worker is available
        notifyAvailable();

        co_return true;
    }

    /**
     * @brief Processes a file read request
     */
    void
    on(ReadFileRequest &request) {
        _is_busy = true;
        qb::io::cout() << "FileWorker " << id() << " processing read request: " << request.filepath.c_str() << std::endl;

        // Create a buffer to store the file content
        auto file_content = std::make_shared<std::vector<char>>();

        // Open and read the file.
        //
        // NOTE: this is `qb::io::async::callback(fn)` — the ONE-argument overload — which runs
        // `fn` INLINE, right now; despite the name it schedules nothing (`qb/io/async/io.h`). It
        // is therefore NOT the lifetime hazard the two-argument `callback(fn, delay)` is, and
        // capturing `this` here is safe: the actor cannot have been destroyed between the call
        // and the body. It is also not asynchronous — the read below blocks this core's loop.
        // What the example really demonstrates is the WORKER-POOL shape: the blocking work is
        // moved off the manager's core onto a worker actor's core, not off the loop.
        qb::io::async::callback([this, request, file_content]() {
            qb::io::sys::file file;
            bool              success = false;
            std::string       error_msg;

            // Try to open the file for reading
            if (file.open(request.filepath.c_str(), O_RDONLY) >= 0) {
                // Get the file size using stat
                struct stat st;
                if (stat(request.filepath.c_str(), &st) == 0) {
                    size_t file_size = static_cast<size_t>(st.st_size);

                    // Resize the buffer to accommodate the content
                    file_content->resize(file_size);

                    // Read the file content
                    ssize_t bytes_read = file.read(file_content->data(), file_size);
                    if (bytes_read >= 0) {
                        // Adjust the size based on bytes actually read
                        if (static_cast<size_t>(bytes_read) < file_size) {
                            file_content->resize(bytes_read);
                        }
                        success = true;
                    } else {
                        error_msg = "Read error: ";
                        error_msg += strerror(errno);
                    }
                } else {
                    error_msg = "Unable to get file size: ";
                    error_msg += strerror(errno);
                }
                file.close();
            } else {
                error_msg = "Unable to open file: ";
                error_msg += strerror(errno);
            }

            // Send the response to the requesting actor
            push<ReadFileResponse>(request.requestor, request.filepath.c_str(), file_content, success, success ? "" : error_msg.c_str(),
                                   request.request_id);

            // Signal that this worker is available again
            _is_busy = false;
            notifyAvailable();
        });
    }

    /**
     * @brief Processes a file write request
     */
    void
    on(WriteFileRequest &request) {
        _is_busy = true;
        qb::io::cout() << "FileWorker " << id() << " processing write request: " << request.filepath.c_str() << std::endl;

        // One-argument overload: runs INLINE (see the note in on(ReadFileRequest&)).
        qb::io::async::callback([this, request]() {
            qb::io::sys::file file;
            bool              success       = false;
            size_t            bytes_written = 0;
            std::string       error_msg;

            // Try to open the file for writing
            if (file.open(request.filepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644) >= 0) {
                // Write the content to the file
                ssize_t write_result = file.write(request.data->data(), request.data->size());

                if (write_result >= 0) {
                    bytes_written = static_cast<size_t>(write_result);
                    success       = (bytes_written == request.data->size());
                    if (!success) {
                        error_msg = "Partial write: ";
                        error_msg += std::to_string(bytes_written);
                        error_msg += " / ";
                        error_msg += std::to_string(request.data->size());
                    }
                } else {
                    error_msg = "Write error: ";
                    error_msg += strerror(errno);
                }
                file.close();
            } else {
                error_msg = "Unable to open file: ";
                error_msg += strerror(errno);
            }

            // Send the response to the requesting actor
            push<WriteFileResponse>(request.requestor, request.filepath.c_str(), bytes_written, success, success ? "" : error_msg.c_str(),
                                    request.request_id);

            // Signal that this worker is available again
            _is_busy = false;
            notifyAvailable();
        });
    }

    /**
     * @brief Stops this actor
     */
    void
    on(qb::KillEvent &) {
        qb::io::cout() << "FileWorker " << id() << " shutting down" << std::endl;
        kill();
    }

private:
    /**
     * @brief Notifies the manager that this worker is available
     */
    void
    notifyAvailable() {
        if (!_is_busy) {
            push<WorkerAvailable>(_manager_id, id());
        }
    }
};

} // namespace file_processor