/**
 * @file examples/io/example1_async_io.cpp
 * @example Asynchronous I/O Fundamentals with QB-IO
 *
 * @brief This example demonstrates core asynchronous capabilities of the QB-IO library,
 * including timer-driven operations, basic file I/O, and file system event watching.
 * It is designed to run as a standalone application, showcasing `qb-io` features
 * outside of the full `qb-core` actor system.
 *
 * @details
 * The example comprises three main components:
 * 1.  `FileProcessor`:
 *     -   Inherits from `qb::io::async::with_timeout<FileProcessor>` to implement periodic actions.
 *     -   On each timer expiration (`on(qb::io::async::event::timer const&)`), it alternates
 *         between writing to and reading from a test file (`qb_io_example.txt`).
 *     -   File operations are performed using `qb::io::system::file` (synchronous file API),
 *         but their execution is driven by asynchronous timer events.
 *     -   Reschedules itself using `updateTimeout()`.
 * 2.  `TimerDemonstration`:
 *     -   Also inherits from `qb::io::async::with_timeout<TimerDemonstration>`.
 *     -   Provides a simple demonstration of a recurring timer that logs a "tick" message
 *         at regular intervals.
 * 3.  `FileWatcher`:
 *     -   Uses `ev::stat` (a `libev` watcher integrated with QB-IO's event loop via
 *         `qb::io::async::listener::current.loop()`) to monitor the test file for attribute changes
 *         (e.g., size, modification time).
 *     -   Logs detected changes to the console. (For a higher-level QB abstraction, one might use
 *         `qb::io::async::directory_watcher`).
 *
 * The `main` function:
 * - Initializes the QB asynchronous I/O system for the current thread using `qb::io::async::init()`.
 * - Sets up signal handling for graceful shutdown (Ctrl+C).
 * - Creates instances of `FileProcessor`, `TimerDemonstration`, and `FileWatcher`.
 * - Runs the QB event loop using `qb::io::async::run(EVRUN_NOWAIT)` in a loop for a
 *   fixed duration or until a termination signal is received.
 * - Cleans up the test file upon completion.
 *
 * QB-IO Features Demonstrated:
 * - Asynchronous System Initialization: `qb::io::async::init()`.
 * - Event Loop Integration: `qb::io::async::run()`.
 * - Timer-Based Operations: `qb::io::async::with_timeout<T>`, `on(qb::io::async::event::timer const&)`, `updateTimeout()`.
 * - Basic File I/O: `qb::io::system::file` for synchronous file read/write operations, orchestrated asynchronously.
 * - Low-Level File Watching: Direct use of `ev::stat` with `qb::io::async::listener::current.loop()` to monitor file attribute changes.
 * - Thread-Safe Output: `qb::io::cout()` and `qb::io::cerr()`.
 */

#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/system/file.h>
#include <csignal>

namespace {
    // Global constants for the example
    constexpr const char* TEST_FILENAME = "qb_io_example.txt";
    constexpr int MAX_OPERATIONS = 5;
    constexpr double TIMER_INTERVAL = 1.0; // seconds
    
    // Global atomic flag for termination
    std::atomic<bool> g_running{true};
    
    // Utility function to get timestamp string
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::string timestamp = std::ctime(&time_t);
        // Remove trailing newline from ctime result
        if (!timestamp.empty() && timestamp.back() == '\n') {
            timestamp.pop_back();
        }
        return timestamp;
    }
    
    // Utility function to print a section header
    void printSectionHeader(const std::string& title) {
        qb::io::cout() << "\n=== " << title << " ===\n";
    }
    
    // Signal handler for graceful termination
    void signalHandler(int signal) {
        printSectionHeader("Signal Received");
        qb::io::cout() << "Caught signal " << signal << " ("
                  << (signal == SIGINT ? "SIGINT" : "SIGTERM") << ")" << std::endl;
        g_running = false;
    }
}

/**
 * @brief Event structure for file content
 * 
 * This structure is used to pass file content and metadata 
 * between different components of the application.
 */
struct FileContentEvent {
    std::string content;
    std::string filename;
    bool success;
    
    FileContentEvent(const std::string& fname, const std::string& data, bool ok)
        : content(data), filename(fname), success(ok) {}
};

/**
 * @brief File processor class that demonstrates async I/O operations
 * 
 * This class shows how to:
 * - Schedule operations with timers
 * - Perform file I/O operations asynchronously
 * - Use the QB async framework for event handling
 */
class FileProcessor : public qb::io::async::with_timeout<FileProcessor> {
private:
    std::string _filename;
    int _operation_count = 0;
    const int _max_operations;
    std::string _content;
    
public:
    explicit FileProcessor(const std::string& filename, int max_ops = MAX_OPERATIONS)
        : with_timeout(TIMER_INTERVAL), // 1 second timeout
          _filename(filename),
          _max_operations(max_ops) {
        printSectionHeader("File Processor Initialized");
        qb::io::cout() << "Processor will handle " << _max_operations << " operations on file: "
                  << _filename << std::endl;
    }
    
    /**
     * @brief Timer event handler - called when the timer expires
     * 
     * This function is automatically called when the timer expires.
     * It alternates between reading from and writing to the file.
     */
    void on(qb::io::async::event::timer const&) {
        qb::io::cout() << "FileProcessor: Timer event received at " << getCurrentTimestamp() << std::endl;
        
        // Check termination conditions
        if (_operation_count >= _max_operations || !g_running) {
            qb::io::cout() << "FileProcessor: "
                      << (_operation_count >= _max_operations ? 
                         "Completed all operations" : "Termination requested")
                      << ", stopping" << std::endl;
            return;
        }
        
        // Alternate between read and write operations
        if (_operation_count % 2 == 0) {
            writeToFile();
        } else {
            readFromFile();
        }
        
        _operation_count++;
        
        // Schedule the next operation
        updateTimeout();
    }
    
    /**
     * @brief Write data to the file
     * 
     * Demonstrates how to write to a file using the qb::io::sys::file API.
     * This method creates or truncates the file and writes new content to it.
     */
    void writeToFile() {
        printSectionHeader("Writing to File");
        qb::io::cout() << "FileProcessor: Writing to file: " << _filename << std::endl;
        
        // Generate content with timestamp and operation number
        _content = "Operation #" + std::to_string(_operation_count) + 
                 " at " + getCurrentTimestamp() + "\n" +
                 "This is a test file created by the QB-IO Framework.\n" +
                 "The QB-IO library provides high-performance I/O operations.\n";
        
        // Open file for writing (create if not exists, truncate if exists)
        qb::io::sys::file file;
        if (file.open(_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644) >= 0) {
            // Write content to file
            ssize_t bytes_written = file.write(_content.c_str(), _content.size());
            file.close();
            
            if (bytes_written == static_cast<ssize_t>(_content.size())) {
                qb::io::cout() << "FileProcessor: Successfully wrote " << bytes_written
                          << " bytes to file" << std::endl;
            } else {
                qb::io::cerr() << "FileProcessor: Partial write - only " << bytes_written
                          << " of " << _content.size() << " bytes written" << std::endl;
            }
        } else {
            qb::io::cerr() << "FileProcessor: Error opening file for writing: "
                      << strerror(errno) << std::endl;
        }
    }
    
    /**
     * @brief Read data from the file
     * 
     * Demonstrates how to read from a file using the qb::io::sys::file API.
     * This method opens the file in read-only mode and displays its contents.
     */
    void readFromFile() {
        printSectionHeader("Reading from File");
        qb::io::cout() << "FileProcessor: Reading from file: " << _filename << std::endl;
        
        qb::io::sys::file file;
        if (file.open(_filename, O_RDONLY) >= 0) {
            // Allocate buffer for file content
            char buffer[4096] = {0};
            
            // Read file content
            int bytes_read = file.read(buffer, sizeof(buffer) - 1);
            file.close();
            
            if (bytes_read > 0) {
                // Ensure null termination
                buffer[bytes_read] = '\0';
                qb::io::cout() << "FileProcessor: Successfully read " << bytes_read << " bytes" << std::endl;
                qb::io::cout() << "File content:\n" << std::string(40, '-') << std::endl;
                qb::io::cout() << buffer << std::endl;
                qb::io::cout() << std::string(40, '-') << std::endl;
            } else if (bytes_read == 0) {
                qb::io::cout() << "FileProcessor: File is empty" << std::endl;
            } else {
                qb::io::cerr() << "FileProcessor: Error reading file: "
                          << strerror(errno) << std::endl;
            }
        } else {
            qb::io::cerr() << "FileProcessor: Error opening file for reading: "
                      << strerror(errno) << std::endl;
        }
    }
};

/**
 * @brief Timer demonstration class
 * 
 * This class shows how to use timers for scheduling periodic tasks.
 * It creates a timer that ticks at regular intervals and demonstrates
 * how to handle timer events in the QB async framework.
 */
class TimerDemonstration : public qb::io::async::with_timeout<TimerDemonstration> {
private:
    int _count = 0;
    const int _max_ticks;
    
public:
    explicit TimerDemonstration(int max_ticks = 10)
        : with_timeout(0.5), // 500ms timeout
          _max_ticks(max_ticks) {
        printSectionHeader("Timer Demonstration Initialized");
        qb::io::cout() << "Timer will tick " << _max_ticks << " times every 500ms" << std::endl;
    }
    
    /**
     * @brief Timer event handler
     * 
     * This function is automatically called when the timer expires.
     * It demonstrates the use of periodic timers for regular tasks.
     */
    void on(qb::io::async::event::timer const&) {
        _count++;
        qb::io::cout() << "TimerDemonstration: Tick #" << _count
                  << " at " << getCurrentTimestamp() << std::endl;
        
        // Check termination conditions
        if (_count < _max_ticks && g_running) {
            // Continue with the next tick
            updateTimeout();
        } else {
            qb::io::cout() << "TimerDemonstration: "
                      << (_count >= _max_ticks ? 
                         "Completed all ticks" : "Termination requested")
                      << ", stopping" << std::endl;
        }
    }
};

/**
 * @brief File watcher class using ev::stat
 * 
 * This class demonstrates how to watch for file system changes.
 * It uses the libev stat watcher to monitor changes to a file
 * and reacts to modifications in real-time.
 */
class FileWatcher {
private:
    std::string _filename;
    ev::stat* _watcher = nullptr;
    
public:
    explicit FileWatcher(const std::string& filename)
        : _filename(filename) {
        printSectionHeader("File Watcher Initialized");
        
        // Create the stat watcher for monitoring file changes
        _watcher = new ev::stat(qb::io::async::listener::current.loop());
        
        // Set callback for file change events
        _watcher->set<FileWatcher, &FileWatcher::onFileChange>(this);
        
        // Set the file to watch
        _watcher->set(_filename.c_str());
        
        // Start watching
        _watcher->start();
        
        qb::io::cout() << "FileWatcher: Started watching file: " << _filename << std::endl;
    }
    
    ~FileWatcher() {
        // Cleanup resources
        if (_watcher) {
            _watcher->stop();
            delete _watcher;
            _watcher = nullptr;
        }
    }
    
    /**
     * @brief Callback for file change events
     * 
     * This function is called whenever the watched file changes.
     * It detects changes in file size and modification time.
     * 
     * @param watcher The stat watcher that triggered the event
     * @param revents Event flags describing what changed
     */
    void onFileChange(ev::stat& watcher, int revents) {
        qb::io::cout() << "\nFileWatcher: Detected change in file: " << _filename
                  << " at " << getCurrentTimestamp() << std::endl;
        
        // Check what changed
        if (revents & ev::STAT) {
            // File size changed
            if (watcher.attr.st_size != watcher.prev.st_size) {
                qb::io::cout() << "FileWatcher: File size changed from "
                          << watcher.prev.st_size << " to " 
                          << watcher.attr.st_size << " bytes" << std::endl;
            }
            
            // Modification time changed
            if (watcher.attr.st_mtime != watcher.prev.st_mtime) {
                qb::io::cout() << "FileWatcher: File modification time changed" << std::endl;
            }
        }
    }
};

int main() {
    printSectionHeader("QB-IO Asynchronous I/O Example");
    qb::io::cout() << "This example demonstrates asynchronous file operations, timers, and file watching" << std::endl;
    
    // Setup signal handling for graceful termination
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Initialize the async I/O system
    qb::io::async::init();
    
    // Create the test file path
    std::string filename = TEST_FILENAME;
    
    // Create components for the demonstration
    FileProcessor processor(filename);
    TimerDemonstration timer_demo;
    FileWatcher watcher(filename);
    
    printSectionHeader("Starting Event Loop");
    qb::io::cout() << "Running event loop for approximately 6 seconds..." << std::endl;
    qb::io::cout() << "Press Ctrl+C to terminate the example early" << std::endl;
    
    // Run the event loop until completion or termination request
    int iterations = 0;
    const int MAX_ITERATIONS = 30; // 30 * 200ms = 6 seconds
    
    while (g_running && iterations < MAX_ITERATIONS) {
        qb::io::async::run(EVRUN_NOWAIT);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        iterations++;
    }
    
    printSectionHeader("Event Loop Completed");
    qb::io::cout() << "Event loop ran for " << (iterations * 200) / 1000.0 << " seconds" << std::endl;
    
    // Clean up the test file
    if (std::remove(filename.c_str()) == 0) {
        qb::io::cout() << "Test file '" << filename << "' was removed successfully" << std::endl;
    } else {
        qb::io::cerr() << "Error removing test file: " << strerror(errno) << std::endl;
    }
    
    printSectionHeader("Example Completed");
    qb::io::cout() << "Demonstrated qb::io async file operations, timers, and file watching" << std::endl;
    
    return 0;
} 