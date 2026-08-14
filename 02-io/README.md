# Tier 02 — `qb-io`, standalone

The asynchronous I/O layer on its own: an event loop, files, sockets and protocols, with **no
actor and no `qb::Main`**. The claim is checkable and the tree checks it — there is no
`qb::Actor`, no `qb::Main` and no `addActor` anywhere in this directory, and the CMake targets
link `qb-io` alone.

**This tier has no prerequisite on `01-actors/`.** A reader who came for an event loop and a
socket can start here. It sits second because most readers came for actors.

## Names

Four names, one source. The tier directory and the source file are written; the CMake **target**
and the produced **binary** are *derived* from them by `qb_example()`
(`examples/cmake/qbExample.cmake`), and `OUTPUT_NAME` is never set — so they cannot disagree
with the file, which is how a previous example came to document a build target that does not
exist.

| Source | Target and binary |
|---|---|
| `01-event-loop.cpp` | `qb-example-io-event-loop` |
| `02-files.cpp` | `qb-example-io-files` |
| `03-tcp.cpp` | `qb-example-io-tcp` |
| `04-udp.cpp` | `qb-example-io-udp` |
| `05-custom-protocol.cpp` | `qb-example-io-custom-protocol` |

Each source also carries a machine-readable header block — `@teaches`, `@demonstrates`,
`@prerequisites`, `@expect` — and `dev/agent/check-example-headers.py` asserts that every name
in `@demonstrates` occurs in that file's **code**, with comments and string literals stripped
first. The block is a promise; the checker is the proof.

## Building and running

```bash
# From the superproject root. Examples are FORCE-built ON there.
cmake --preset release
cmake --build --preset release --target qb-example-io-tcp
./build/presets/release/examples/02-io/qb-example-io-tcp
```

The executables land in `build/presets/<preset>/examples/02-io/`.

---

## 1. The event loop (`01-event-loop.cpp`)

* **Teaches**: initialise the loop, run it, and let a timer, a synchronous file and a filesystem
  watcher share one thread.
* **Details**:
    * **`FileProcessor`**:
        * Uses `qb::io::async::with_timeout<FileProcessor>` for periodic actions. `with_timeout`
          is a **CRTP base**: `class T : public qb::io::async::with_timeout<T>`, with
          `void on(qb::io::async::event::timer const&)` as the expiry handler.
        * Alternates writing to and reading from `qb_io_example.txt` using synchronous
          `qb::io::sys::file` calls — `open`/`read`/`write`/`close` — triggered by timer events.
        * Reschedules itself with **`setTimeout(_interval)`** (`01-event-loop.cpp:179`). Not
          `updateTimeout()`: the watcher that just fired is one-shot and is not re-armed on that
          branch, and `updateTimeout()` only stamps the last-activity time — it never restarts
          the timer. The source says so at `:176-178`. `TimerDemonstration` re-arms the same way
          at `:291`.
    * **`TimerDemonstration`**: the same mixin, showing a recurring timer logging "tick".
    * **`FileWatcher`**: drops to `ev::stat` (a raw `libev` watcher) through
      `qb::io::async::listener::current.loop()`, to monitor one path for attribute changes. For a
      higher-level abstraction see `qb::io::async::directory_watcher`.
    * **`main`**: `qb::io::async::init()`, signal handling, then
      `qb::io::async::run(EVRUN_NOWAIT)` in a loop for a fixed duration.
* **Run**: `./build/presets/release/examples/02-io/qb-example-io-event-loop`

---

## 2. Files (`02-files.cpp`)

* **Teaches**: qb's synchronous file handle, and where it ends.
* **Details**: `FileOperationsManager` writes and reads a binary file through
  `qb::io::sys::file` with `open`/`read`/`write`/`close` and measures throughput; then does the
  same region with POSIX `mmap()`/`msync()`, copies a file in chunks, and reads metadata with
  POSIX `stat()`. The point of the second half is that **qb wraps none of it** — `mmap`, `stat`
  and `std::filesystem` are the platform's, not qb's.
* **Not built on Windows**: the memory-mapping half has no equivalent here, so the target is not
  created rather than failing to compile.
* **Run**: `./build/presets/release/examples/02-io/qb-example-io-files`

---

## 3. TCP (`03-tcp.cpp`)

* **Teaches**: a server, its per-connection session and a client, all as CRTP roles over one loop.
* **Details**:
    * **`TCPServer`** inherits `qb::io::use<TCPServer>::tcp::server<ServerClientHandler>` and
      listens with `transport().listen_v4()`. The session-pool mixin creates a
      `ServerClientHandler` per accepted connection.
    * **`ServerClientHandler`** inherits `qb::io::use<ServerClientHandler>::tcp::client<TCPServer>`
      — the server-side session is itself a client role, parameterised by its owner.
    * **`TCPClient`** inherits `qb::io::use<TCPClient>::tcp::client<>` and connects with
      `transport().connect_v4()`.
    * All three frame with the shipped newline protocol,
      `using Protocol = qb::protocol::text::command<HandlerType>`: `on(Protocol::message&&)`
      receives, and `*this << msg << Protocol::end` sends.
* **Run**: `./build/presets/release/examples/02-io/qb-example-io-tcp` — the server starts, the
  client connects, sends its commands, and both shut down.

---

## 4. UDP (`04-udp.cpp`)

* **Teaches**: the datagram shape of the same roles.
* **Details**:
    * **`UDPServer`** inherits `qb::io::use<UDPServer>::udp::server` and binds with
      `transport().bind_v4()`.
    * **`UDPClient`** inherits `qb::io::use<UDPClient>::udp::client`, initialises its socket with
      `transport().init()`, and names a destination per send with
      `setDestination(qb::io::endpoint().as_in(host, port))` — `qb::io::endpoint` is the socket
      address builder.
    * Both use `qb::protocol::text::command<HandlerType>` over datagrams.
* **Run**: `./build/presets/release/examples/02-io/qb-example-io-udp`

---

## 5. A protocol qb does not ship (`05-custom-protocol.cpp`)

* **Teaches**: writing a wire format end to end — framing, serialisation, dispatch.
* **Details**:
    * **Framing and dispatch**: `class custom_protocol : public io::async::AProtocol<IO_>`
      (written inside `namespace qb`, so the full name is `qb::io::async::AProtocol<IO_>`).
      `getMessageSize()` validates the 12-byte header and returns the whole frame's size;
      `onMessage()` rebuilds a `qb::custom_message` and hands it to the handler.
    * **Serialisation** is a specialisation of the output pipe:
      `template<> qb::allocator::pipe<char>& qb::allocator::pipe<char>::put<qb::custom_message>(const qb::custom_message&)`
      — the customisation point a user specialises to send a custom type.
    * `switch_protocol<T>()` swaps the protocol on a live connection.
* **Run**: two terminals.
  ```bash
  ./build/presets/release/examples/02-io/qb-example-io-custom-protocol server 9876
  ./build/presets/release/examples/02-io/qb-example-io-custom-protocol client 127.0.0.1 9876
  ```
  The client sends `HELLO`, then echoes whatever you type. `quit` exits.

---

## What this tier does not yet cover

The design for this tier has eleven programs; five exist. The six that are not written here are
`06-framing-toolbox`, `07-tls`, `08-timeouts-and-watchers`, `09-graceful-drain`,
`10-crypto-and-compression` and `11-logging-and-metrics` — so `qb::io::uri`, the TLS transports,
`file_watcher`/`directory_watcher`, the `eof`/`eos`/`dispose` drain vocabulary, `qb::crypto`,
`qb::jwt`, `qb::compression` and the `QB_LOG_*` logger have no demonstrator in this tier today.
Nothing here should be read as saying they do not exist.
