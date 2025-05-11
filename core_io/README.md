# QB Core & IO Integration Examples

This directory contains examples that demonstrate the integration of `qb-core` (Actor model, concurrency) and `qb-io` (asynchronous I/O, networking, utilities) to build more complex, networked, and concurrent applications.

Each subdirectory represents a mini-project showcasing a specific use case.

## Building the Examples

To build these examples, navigate to the root of the QB Framework and use CMake.

```bash
# From the root directory of the qb-framework
mkdir build && cd build
cmake ..
# To build a specific example, refer to its individual README for target names.
# e.g., cmake --build . --target chat_server
# Or build all examples
cmake --build .
```
Executables will typically be located in a subdirectory within `build/examples/core_io/`.

## Mini-Project Examples

Detailed explanations, QB features demonstrated, and specific build/run instructions for each mini-project can be found in their respective README files:

1.  **TCP Chat System (`chat_tcp/`)**
    *   Implements a multi-client, multi-core TCP chat application.
    *   Demonstrates actor-based server architecture, custom protocols, and session management.
    *   [Detailed README](./chat_tcp/README.md)

2.  **File Monitor (`file_monitor/`)**
    *   A real-time file system monitoring application using actors and `qb-io`'s asynchronous file watching.
    *   Shows how to detect and process file system changes (creations, modifications, deletions).
    *   [Detailed README](./file_monitor/README.md)

3.  **File Processor (`file_processor/`)**
    *   Illustrates a distributed file processing system using a manager-worker actor pattern.
    *   Demonstrates offloading blocking file I/O operations to worker actors.
    *   [Detailed README](./file_processor/README.md)

4.  **Message Broker (`message_broker/`)**
    *   Implements a topic-based publish-subscribe message broker.
    *   Showcases a scalable server architecture, custom binary protocol, and zero-copy message handling techniques.
    *   [Detailed README](./message_broker/README.md)

Please refer to the individual README files within each sub-project directory for more in-depth information. 