# QB Framework - Code Examples

Welcome to the QB Framework examples directory! This collection showcases the capabilities of the QB C++ Actor Framework and its associated modules. These examples are designed to help you understand how to use various components, from core actor functionalities to specialized modules for networking, database interaction, and more.

## Example Categories

Below is an overview of the different categories of examples available. Each category has its own detailed README with information on building and running the specific examples it contains.

### 1. QB Core Examples (`./core/`)

These examples focus on the fundamental concepts and features of the `qb-core` library, which provides the actor model implementation and concurrency management. You'll find demonstrations of:
- Basic actor creation, communication, and lifecycle.
- Event-driven programming.
- Multi-core actor deployment.
- Common actor patterns (supervisor-worker, pub/sub, FSM).

[**Dive into QB Core Examples &raquo;**](./core/README.md)

### 2. QB Core & IO Integration Examples (`./core_io/`)

This section contains mini-projects that illustrate how to combine `qb-core` (actor model) with `qb-io` (asynchronous I/O, networking utilities) to build more complex, networked applications. Examples include:
- TCP Chat System
- File System Monitor
- Distributed File Processor
- Message Broker

[**Explore QB Core & IO Integration Examples &raquo;**](./core_io/README.md)

### 3. QB Module (QBM) Examples (`./qbm/`)

The `qbm` directory houses examples for various specialized modules that extend the QB Framework's functionality. Each module provides a client or utilities for specific services or protocols.
- **HTTP**: Examples for building HTTP/HTTPS/HTTP2 servers and clients.
- **PostgreSQL**: Demonstrations of asynchronous interaction with PostgreSQL databases.
- **Redis**: Examples for using Redis for caching, messaging (Pub/Sub), and data storage.

[**Discover QB Module Examples &raquo;**](./qbm/README.md)

## Getting Started

To build and run any of these examples:
1. Ensure the QB Framework and its dependencies are correctly built and installed.
2. Navigate to the specific example's directory or its parent category's README for detailed build and execution instructions. Most examples use CMake.

We encourage you to explore these examples to gain a practical understanding of the QB Framework's power and flexibility. 