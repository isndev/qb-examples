# Example: TCP Message Broker (`message_broker`)

This example implements a topic-based publish-subscribe message broker using the QB C++ Actor Framework. It demonstrates a scalable server architecture with a custom binary protocol for efficient client-server communication and zero-copy message handling techniques for broadcasts.

## Overview

The system allows clients to connect, subscribe to topics, publish messages to topics, and receive messages from topics they are subscribed to.

**Server-Side Architecture:**
*   **`AcceptActor` (`server/AcceptActor.h/.cpp`)**: Listens for incoming TCP client connections on a configured port (e.g., 12345). Upon connection, it distributes the new client socket to one of the `ServerActor`s in a pool using a round-robin strategy.
*   **`ServerActor` (`server/ServerActor.h/.cpp`)**: Multiple instances form a pool. Each `ServerActor` manages several client sessions (`BrokerSession`). It acts as an intermediary:
    *   Receives raw commands (SUBSCRIBE, UNSUBSCRIBE, PUBLISH) from its `BrokerSession`s.
    *   Packages these into QB events (`SubscribeEvent`, `UnsubscribeEvent`, `PublishEvent`) and forwards them to the `TopicManagerActor`.
    *   Receives `SendMessageEvent`s from the `TopicManagerActor` (which contain messages to be delivered to specific clients) and sends the data to the appropriate `BrokerSession`.
*   **`BrokerSession` (`server/BrokerSession.h/.cpp`)**: Not an actor, but a class managed by `ServerActor`. Each instance handles the I/O and protocol parsing for a single connected client using the custom `BrokerProtocol`. It parses incoming client commands and delegates them to its parent `ServerActor`. It also handles session timeouts.
*   **`TopicManagerActor` (`server/TopicManagerActor.h/.cpp`)**: The central brain of the broker.
    *   Manages topics and the set of client sessions subscribed to each topic.
    *   Handles `SubscribeEvent` and `UnsubscribeEvent` to update subscription lists.
    *   Processes `PublishEvent`: when a message is published to a topic, it identifies all subscribed sessions and creates `SendMessageEvent`s, `push`ing them to the respective `ServerActor`s that manage those sessions.
    *   Handles `DisconnectEvent` to clean up subscriptions for a disconnected client.
    *   Employs `broker::MessageContainer` to enable efficient, zero-copy broadcasting of published messages.

**Client-Side Architecture (`client/`):**
*   **`ClientActor`**: Connects to the broker server, sends user commands (subscribe, publish, unsubscribe) using `BrokerProtocol`, and displays messages received from the server.
*   **`InputActor`**: Handles console input, allowing the user to type commands like `SUB topic`, `PUB topic message`, etc.

**Shared Components (`shared/` directory):**
*   **`Protocol.h/.cpp` (`BrokerProtocol`)**: Defines the custom binary protocol (header with magic, version, type, length + payload) for client-server communication. Includes serialization (`pipe::put` specialization) and parsing (`AProtocol` implementation) logic.
*   **`Events.h`**: Defines QB events for server-side inter-actor communication (`NewSessionEvent`, `SubscribeEvent`, `PublishEvent`, etc.) and client-side input (`BrokerInputEvent`). Notably, `PublishEvent` and `SendMessageEvent` use `broker::MessageContainer` to facilitate efficient, potentially zero-copy, message payload handling, especially during broadcasts.

## QB Features Demonstrated

*   **Core (`qb-core`)**:
    *   Scalable Actor-Based Architecture: Separation of concerns (accept, session I/O, topic logic) into different actor types.
    *   Multi-Core Deployment: Actors are assigned to different CPU cores.
    *   Advanced Event System: Custom events with `broker::MessageContainer` for zero-copy message passing of payloads via `std::string_view`s in events.
    *   Centralized State Management: `TopicManagerActor` as the single source of truth for subscriptions.
*   **I/O (`qb-io`)**:
    *   Asynchronous TCP Server (`AcceptActor`, `ServerActor` with `BrokerSession`).
    *   Asynchronous TCP Client (`ClientActor`).
    *   Custom Binary Protocol: Full implementation including framing, serialization (`pipe::put`), and parsing (`AProtocol`).
    *   Session Management with Timeouts (`BrokerSession`).
    *   `qb::io::uri`, `qb::io::async::callback` (client reconnection), `qb::ICallback` (client input).

## Zero-Copy Message Handling

A key feature of this example is the optimized handling of message payloads, especially for published messages that need to be broadcast to multiple subscribers.
1.  When a `BrokerSession` receives a PUBLISH command, it creates a `broker::MessageContainer` which takes ownership of the message payload.
2.  It then creates `std::string_view`s that point to the topic and content within this container.
3.  These views, along with the moved `MessageContainer`, are passed to `ServerActor::handlePublish`.
4.  `ServerActor` constructs a `PublishEvent` that also takes ownership of the `MessageContainer` and includes these `string_view`s. This event is sent to `TopicManagerActor`.
5.  `TopicManagerActor`, upon receiving the `PublishEvent`, formats the message for delivery. It then creates *one* `broker::MessageContainer` for the final broadcast message.
6.  For each subscriber, it creates a `SendMessageEvent` that holds a *shared reference* (via `std::shared_ptr` internal to `MessageContainer`) to this single broadcast `MessageContainer`.
7.  These `SendMessageEvent`s are sent to the respective `ServerActor`s, which then send the data from the shared container to their clients.

This ensures that the actual message payload is stored once and shared among all recipients during a broadcast, minimizing data copies and improving performance.

## How to Build and Run

1.  **Build**:
    In your QB framework `build` directory:
    ```bash
    cmake --build . --target broker_server
    cmake --build . --target broker_client
    ```
    Executables will be in `build/examples/core_io/message_broker/server/` and `build/examples/core_io/message_broker/client/`.

2.  **Run Server**:
    ```bash
    ./broker_server
    ```
    Listens on port 12345 by default.

3.  **Run Client(s)**:
    ```bash
    ./broker_client <host> <port>
    ```
    Example: `./broker_client 127.0.0.1 12345`
    Commands:
    *   `SUB <topic_name>`
    *   `UNSUB <topic_name>`
    *   `PUB <topic_name> <your message content>`
    *   `help`
    *   `quit` 