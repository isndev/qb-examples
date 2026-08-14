# Example: File Monitoring System (`file_monitor`)

This example demonstrates a real-time file system monitoring application built using QB C++ Actor Framework, integrating
`qb-core` for actor-based concurrency and `qb-io` for asynchronous file system watching.

## Overview

The system monitors a specified directory (and optionally its subdirectories) for changes such as file creation,
modification, and deletion. It uses a dedicated actor for watching and another for processing these events. A client
actor simulates file operations to test the system.

**Key Components:**

* **`DirectoryWatcher` Actor (`watcher.h/.cpp`)**:
    * The core monitoring component. It receives requests (`WatchDirectoryRequest`) to monitor specific paths.
    * Internally, it uses `qb::io::async::directory_watcher`, which wraps `ev::stat` (`qb/src/qb/io/async/io.h:719`).
      That is a periodic `stat()` of the watched path at the interval passed to `start()` (default 100 ms,
      `io.h:745`), accelerated by `inotify` on Linux when the kernel provides it (`qb/src/qb/vendor/qev/qev.c:5309`).
      There is **no** FSEvents backend — on macOS and the BSDs it is the polling timer alone, so a change is
      observed at the next tick, not instantly.
    * When a change is detected, it determines the type of event (CREATED, MODIFIED, DELETED, ATTRIBUTES_CHANGED) and
      creates a `FileEvent`.
    * It `push`es this `FileEvent` to all actors that have subscribed to watch that particular path.
    * Handles `UnwatchDirectoryRequest` to stop monitoring a path for a specific subscriber or altogether if no
      subscribers remain.
* **`FileProcessor` Actor (`processor.h/.cpp`)**:
    * Responsible for acting upon detected file changes.
    * **As wired, it never receives a `FileEvent`.** `DirectoryWatcher` pushes one only to the subscribers of a watched
      path, and a subscriber is whoever sent a `WatchDirectoryRequest` (`watcher.cpp:136-138`). Only `ClientActor` ever
      sends one (`main.cpp:178`), so `FileProcessor::on(FileEvent&)` — registered at `processor.cpp:48` — is dead code
      in this program. To wake it up, have it `push<WatchDirectoryRequest>(watcher_id, dir, recursive, id())` from its
      own `onInit()`, exactly as `ClientActor::startMonitoring()` does; it will then be added to the same subscriber
      list and receive every event.
    * For CREATED/MODIFIED events, it can extract file metadata (size, simple content hash). It maintains a cache of
      known file metadata to differentiate between actual content modifications and mere timestamp updates.
    * For DELETED events, it updates its internal tracking.
    * Can be configured (e.g., to ignore hidden files) via `SetProcessingConfigRequest`.
    * Can report `ProcessingStats`.
* **`ClientActor` (`main.cpp`)**:
    * Acts as a test harness and a subscriber.
    * Sends `WatchDirectoryRequest` to `DirectoryWatcher` to start monitoring a test directory.
    * Receives `WatchDirectoryResponse` to confirm.
    * Drives the test load: `scheduleRandomModifications()` (`main.cpp:290-315`) picks one of create / modify / delete
      at random, performs it, then re-arms itself 0.5–1.5 s later — a self-rescheduling chain, not a repeating timer.
      Five files are created up front (`main.cpp:191-193`).
    * Receives and logs `FileEvent`s for the directory it is watching.
    * After a configurable duration, it initiates a system-wide shutdown by broadcasting `qb::KillEvent`
      (`main.cpp:199-206`).
* **Shared Events (`events.h`)**:
    * Defines various `qb::Event` derived structs (`FileEvent`, `WatchDirectoryRequest`, `WatchDirectoryResponse`, etc.)
      and supporting enums/structs (`FileEventType`, `FileMetadata`) for communication between actors.

> **Do not copy the timer shape this file uses.** Three of its `qb::io::async::callback(f, delay)` calls capture
> `this` — `main.cpp:125` (deferred start), `main.cpp:199-206` (end-of-test kill) and `main.cpp:314` (the
> reschedule chain). A delayed `callback` heap-allocates a `Timeout` owned by the **event loop**, not by the actor
> (`qb/src/qb/io/async/io.h:389`), and nothing cancels it when the actor dies. If the actor is destroyed before the
> timer fires, the lambda dereferences freed memory — and an `if (_is_running)` guard like the one at `main.cpp:201`
> and `main.cpp:291` does **not** save you: `_is_running` is a member, so reading it *is* the use-after-free. The
> same shape in `examples/core/example10_distributed_computing.cpp` aborted under AddressSanitizer.
>
> The lifetime-bound replacement, which every example under `examples/core/` now uses:
>
> ```cpp
> spawn([copies_by_value](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
>     co_await ctx.sleep(std::chrono::milliseconds(500));
>     ctx.template push<SomeTickEvent>();   // back in actor context
> });
> ```
>
> `spawn` is `qb/src/qb/core/Actor.h:1238-1239`, and `Actor::kill()` cancels its scope at
> `qb/src/qb/core/Actor.cpp:283-289`. Never capture `this`. If you must keep a raw timer, use
> `qb::io::async::scoped_callback(f, d)` (`io.h:479-484`) and hold the returned `unique_ptr` as an actor member —
> destroying it cancels the pending callback.

## QB Features Demonstrated

* **Core (`qb-core`)**:
    * `qb::Actor`: For all main components (`DirectoryWatcher`, `FileProcessor`, `ClientActor`).
    * `qb::Main`: For engine setup and lifecycle.
    * Multi-Core Deployment: Actors are assigned to different cores (`engine.addActor<T>(core_id, ...)`).
    * `qb::Event`: Custom events for typed, asynchronous inter-actor communication.
    * `push<Event>(...)`: Sending events between actors.
    * `broadcast<qb::KillEvent>()`: For system-wide shutdown.
    * Actor State Management: `DirectoryWatcher` managing watch configurations and subscribers; `FileProcessor` tracking
      file metadata.
* **I/O (`qb-io`)**:
    * `qb::io::async::directory_watcher`: The core mechanism for asynchronous file system monitoring, used within the
      `DirectoryWatcher` actor. Its `start()` (like `file_watcher::start()`) takes the watched path as a
      `std::filesystem::path`; the watcher copies it into a string it owns for the watcher's lifetime, because the
      underlying `ev::stat` keeps the path pointer without copying.
    * `qb::io::async::event::file`: The low-level event structure provided by `directory_watcher`.
    * `qb::io::async::callback`: **Two different things, depending on the argument count.**
        * With a duration (`main.cpp:125`, `199`, `314`) it is a real timer — and the hazard described above.
        * With **one** argument it schedules nothing: the function is invoked **inline, synchronously**, right where
          you call it (`qb/src/qb/io/async/io.h:366-370`). That is the form used at `watcher.cpp:141` and at
          `main.cpp:217`, `245`, `274`, so the directory scan and the test-file writes are **not** deferred and **not**
          off the actor's thread — they run in the middle of the current handler. If you want "next loop turn", the
          primitive is `qb::io::async::defer(fn)` (`qb/src/qb/io/async/listener.h:1030-1034`).
    * `qb::io::sys::file` (namespace `qb::io::sys`, not `qb::io::system` — `qb/src/qb/io/system/file.h:44`): used by
      `ClientActor` to perform synchronous file manipulations for testing and by `FileProcessor` to read file contents.
    * Thread-Safe Console I/O: `qb::io::cout()`, `qb::io::cerr()`.

## How to Build and Run

1. **Build**:
   Navigate to the main `build` directory of your QB framework checkout.
   Ensure CMake has been run from the root.
   Build the `file_monitor` target:
   ```bash
   cmake --build . --target file_monitor
   ```
   The executable will be found in `build/examples/core_io/file_monitor/`.

2. **Run**:
   Execute the program from the build directory:
   ```bash
   ./file_monitor [test_directory_path] [duration_seconds]
   ```
    * `test_directory_path` (optional): The path to the directory that will be created and monitored. Defaults to
      `./monitor_test_files` relative to where you run the executable.
    * `duration_seconds` (optional): How long the `ClientActor` will perform test file operations before initiating
      shutdown. Defaults to 30 seconds.

   Example:
   ```bash
   ./file_monitor ./my_test_watch_dir 60
   ```
   The application will output logs to the console showing watch setup, file operations performed by the `ClientActor`,
   events detected by `DirectoryWatcher`, and processing actions by `FileProcessor`. After the duration it shuts down:
   `ClientActor` unwatches the directory and broadcasts `qb::KillEvent` (`main.cpp:157-167`, `main.cpp:203`).

   It does **not** delete anything. The test directory and whatever `test_file_*.txt` survived the random
   create/modify/delete pass are still on disk when the process exits — nothing in the program removes them. Clean up
   by hand (`rm -rf ./monitor_test_files`) if you care. 