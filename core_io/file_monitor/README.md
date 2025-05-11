# Example: File Monitoring System (`file_monitor`)

This example demonstrates a real-time file system monitoring application built using QB C++ Actor Framework, integrating `qb-core` for actor-based concurrency and `qb-io` for asynchronous file system watching.

## Overview

The system monitors a specified directory (and optionally its subdirectories) for changes such as file creation, modification, and deletion. It uses a dedicated actor for watching and another for processing these events. A client actor simulates file operations to test the system.

**Key Components:**
*   **`DirectoryWatcher` Actor (`watcher.h/.cpp`)**:
    *   The core monitoring component. It receives requests (`WatchDirectoryRequest`) to monitor specific paths.
    *   Internally, it uses `qb::io::async::directory_watcher` (which wraps OS-level mechanisms like `inotify` on Linux or `FSEvents` on macOS) to get notifications about file system changes.
    *   When a change is detected, it determines the type of event (CREATED, MODIFIED, DELETED, ATTRIBUTES_CHANGED) and creates a `FileEvent`.
    *   It `push`es this `FileEvent` to all actors that have subscribed to watch that particular path.
    *   Handles `UnwatchDirectoryRequest` to stop monitoring a path for a specific subscriber or altogether if no subscribers remain.
*   **`FileProcessor` Actor (`processor.h/.cpp`)**:
    *   Responsible for acting upon detected file changes.
    *   It receives `FileEvent`s (from `DirectoryWatcher`, assuming it has subscribed or events are broadcast to it).
    *   For CREATED/MODIFIED events, it can extract file metadata (size, simple content hash). It maintains a cache of known file metadata to differentiate between actual content modifications and mere timestamp updates.
    *   For DELETED events, it updates its internal tracking.
    *   Can be configured (e.g., to ignore hidden files) via `SetProcessingConfigRequest`.
    *   Can report `ProcessingStats`.
*   **`ClientActor` (`main.cpp`)**:
    *   Acts as a test harness and a subscriber.
    *   Sends `WatchDirectoryRequest` to `DirectoryWatcher` to start monitoring a test directory.
    *   Receives `WatchDirectoryResponse` to confirm.
    *   Uses `qb::io::async::callback` to periodically create, modify, and delete files in the test directory, thereby generating events for the system to process.
    *   Receives and logs `FileEvent`s for the directory it is watching.
    *   After a configurable duration, it initiates a system-wide shutdown by broadcasting `qb::KillEvent`.
*   **Shared Events (`events.h`)**:
    *   Defines various `qb::Event` derived structs (`FileEvent`, `WatchDirectoryRequest`, `WatchDirectoryResponse`, etc.) and supporting enums/structs (`FileEventType`, `FileMetadata`) for communication between actors.

## QB Features Demonstrated

*   **Core (`qb-core`)**:
    *   `qb::Actor`: For all main components (`DirectoryWatcher`, `FileProcessor`, `ClientActor`).
    *   `qb::Main`: For engine setup and lifecycle.
    *   Multi-Core Deployment: Actors are assigned to different cores (`engine.addActor<T>(core_id, ...)`).
    *   `qb::Event`: Custom events for typed, asynchronous inter-actor communication.
    *   `push<Event>(...)`: Sending events between actors.
    *   `broadcast<qb::KillEvent>()`: For system-wide shutdown.
    *   Actor State Management: `DirectoryWatcher` managing watch configurations and subscribers; `FileProcessor` tracking file metadata.
*   **I/O (`qb-io`)**:
    *   `qb::io::async::directory_watcher`: The core mechanism for asynchronous file system monitoring, used within the `DirectoryWatcher` actor.
    *   `qb::io::async::event::file`: The low-level event structure provided by `directory_watcher`.
    *   `qb::io::async::callback`: Used by `ClientActor` to schedule test file operations and by `DirectoryWatcher` to defer setup.
    *   `qb::io::system::file`: Used by `ClientActor` to perform synchronous file manipulations for testing and by `FileProcessor` to read file contents.
    *   Thread-Safe Console I/O: `qb::io::cout()`, `qb::io::cerr()`.

## How to Build and Run

1.  **Build**:
    Navigate to the main `build` directory of your QB framework checkout.
    Ensure CMake has been run from the root.
    Build the `file_monitor` target:
    ```bash
    cmake --build . --target file_monitor
    ```
    The executable will be found in `build/examples/core_io/file_monitor/`.

2.  **Run**:
    Execute the program from the build directory:
    ```bash
    ./file_monitor [test_directory_path] [duration_seconds]
    ```
    *   `test_directory_path` (optional): The path to the directory that will be created and monitored. Defaults to `./monitor_test_files` relative to where you run the executable.
    *   `duration_seconds` (optional): How long the `ClientActor` will perform test file operations before initiating shutdown. Defaults to 30 seconds.

    Example:
    ```bash
    ./file_monitor ./my_test_watch_dir 60
    ```
    The application will output logs to the console showing watch setup, file operations performed by the `ClientActor`, events detected by `DirectoryWatcher`, and processing actions by `FileProcessor`. After the duration, it will clean up and shut down. 