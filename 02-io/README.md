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
| `06-framing-toolbox.cpp` | `qb-example-io-framing-toolbox` |
| `07-tls.cpp` | `qb-example-io-tls` (`REQUIRES ssl`) |
| `08-timeouts-and-watchers.cpp` | `qb-example-io-timeouts-and-watchers` |
| `09-graceful-drain.cpp` | `qb-example-io-graceful-drain` |
| `10-crypto-and-compression.cpp` | `qb-example-io-crypto-and-compression` (`REQUIRES ssl compression`) |
| `11-logging-and-metrics.cpp` | `qb-example-io-logging-and-metrics` |

Two of them declare a capability gate. In a build without OpenSSL they are **not created at all**
— they do not fail to compile, they silently do not exist — so the build's roster records them as
`gated` and `dev/agent/run-examples.py` reports a SKIP naming the capability, instead of a program
that has gone missing.

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

## 6. The framing toolbox (`06-framing-toolbox.cpp`)

* **Teaches**: most wire formats need **no parser**. Six framings — a delimiter byte, a delimiter
  sequence, an 8-bit length prefix, a 32-bit length prefix, JSON and MessagePack — each decoded out
  of **one** `write()` per connection, because TCP has no message boundaries and the archetype is
  what supplies them. (`text::command` is deliberately not a seventh: it is the first of those with
  a different delimiter, and `03-tcp.cpp` already runs it end to end.)
* **Details**: the three archetypes in `qb/io/protocol/base.h` supply `getMessageSize()` (the
  framing) and nothing else; you supply `onMessage()` (the meaning), which is why the two custom
  protocols here are eight lines each. `qb::protocol::text` and `qb::protocol::json` are the
  ready-made pairings of the same two halves. `Header()` writes the length prefix — including the
  `htons`/`htonl` — so nothing in this file spells out an endian swap. Framing is also a **bound**:
  an empty record and a 600-deep JSON payload are both refused with `not_ok()`, which disposes the
  session with `disconnected.reason == -1` and leaves the process alone.
* **Run**: `./build/presets/release/examples/02-io/qb-example-io-framing-toolbox`

---

## 7. TLS (`07-tls.cpp`)

* **Teaches**: `qb::io::ssl::Context` on its own terms, and the beat that matters most — a client
  that does **not** trust the certificate fails to connect.
* **Details**: a plaintext session and a secure one differ by one path component
  (`use<T>::tcp::server<S>` vs `use<T>::tcp::ssl::server<S>`), because TLS is a *transport* here and
  the protocol layer never sees it. `Context` is a value-semantic, reference-counted handle
  configured fluently; `Context::client()` is TLS 1.2+, system trust store, `VerifyMode::peer` with
  no arguments. The shipped development certificate is self-signed with `CA:TRUE`, so the client
  passes it to `trust()` and performs a **real** chain check rather than reaching for
  `set_insecure()`. Section 2 connects a client that trusts only the system store and watches the
  handshake fail; section 3 shows what `set_insecure()` buys and costs. All three are asserted, so
  a run where the untrusted client got in exits non-zero.
* **`REQUIRES ssl`**, and it reads `resources/ssl/cert.pem` relatively — run it from its own
  directory, where the build stages that tree.
* **Run**: `cd build/presets/release/examples/02-io && ./qb-example-io-tls`

---

## 8. Timeouts and watchers (`08-timeouts-and-watchers.cpp`)

* **Teaches**: `with_timeout` is an **inactivity watchdog**, not a periodic timer — proved by
  running the two re-arming calls side by side and counting how often each fires.
* **Details**: `setTimeout(d)` sets the budget, stamps the activity time **and starts the watcher**;
  `updateTimeout()` stamps and nothing else, so it cannot re-arm a timer that has already fired.
  Two otherwise identical objects differ only in which one their handler calls, and the program
  exits non-zero if the counts do not come out. `scoped_callback` is the one-shot you can still
  cancel (`async::callback(f, d)` hands you no handle at all). `use<T>::file` tails a growing file
  with its lines already framed, and `directory_watcher` demonstrates its own limit: both are
  `ev::stat`, which **polls** (libev clamps the interval to ~0.11 s) and reports THAT something
  changed, never WHAT — no filename is carried, because the event is two `struct stat`s.
* **Run**: `./build/presets/release/examples/02-io/qb-example-io-timeouts-and-watchers`

---

## 9. Graceful drain (`09-graceful-drain.cpp`)

* **Teaches**: the shutdown and backpressure vocabulary — `pending_read` / `input_drained` on the
  way in, `pending_write` / `eos` on the way out, `close_after_deliver()`, `disconnected` and
  `dispose` in the order they really run, `extracted`, and `async::defer`.
* **Details**: `input_drained` (historically `eof`) fires on any successful read that empties the
  buffer on a perfectly healthy connection — `disconnected` is the closure event.
  `close_after_deliver()` does not close: it marks the protocol invalid and disposal waits until
  `pendingWrite()` reaches zero, so a farewell queued just before it is delivered in full first
  (and, measured, `eos` does *not* fire on that path — the disposal wins). `dispose()` runs
  `on(disconnected)` first and `on(dispose)` last, which is why a self-owned object may only free
  itself in the latter. The client's reconnect destroys and recreates the very object whose handler
  noticed, from `async::defer` — `async::callback(f)` with no delay runs **inline** and would not
  help. Backpressure is only visible when the buffers are smaller than the message, so the server
  shrinks `SO_SNDBUF` deliberately; the program exits non-zero if `pending_write` never fired.
* **Run**: `./build/presets/release/examples/02-io/qb-example-io-graceful-drain`

---

## 10. Crypto and compression (`10-crypto-and-compression.cpp`)

* **Teaches**: the security and payload primitives qb-io already ships — nothing here includes an
  OpenSSL or zlib header.
* **Details**: digests and HMAC (a digest says the bytes did not change; only an HMAC says who sent
  them), base64, AES-256-GCM that **refuses** a tampered ciphertext, `hash_password()`/
  `verify_password()` (Argon2id, deliberately slow, salt and parameters embedded in the one string
  you store), `constant_time_compare`, `secure_random_fill`, `qb::jwt` used directly rather than
  through HTTP middleware, and `gzip`/`deflate` whose `uncompress` takes an **output bound** — a
  megabyte of zeros is a kilobyte on the wire, and exceeding the bound throws rather than
  truncating.
* **`REQUIRES ssl compression`**.
* **Run**: `./build/presets/release/examples/02-io/qb-example-io-crypto-and-compression`

---

## 11. Logging and metrics (`11-logging-and-metrics.cpp`)

* **Teaches**: the two production surfaces the corpus never used — the asynchronous logger behind
  `QB_LOG_*`, and a fixed-capacity rolling window of measurements taken with the raw CPU counter.
* **Details**: `qb::io::cout()` takes a **process-wide** mutex in its destructor and formats on the
  calling thread; `QB_LOG_*` encodes into a per-thread buffer and returns, and a background thread
  owns the file. The part nobody expects: with `QB_WITH_LOGGING` on (the default) a static
  initialiser in `qb/src/qb/io/logger.cpp` has **already** called `log::init("./qb", 512)` before
  `main()` runs, so every qb binary creates `./qb.1.log` in its working directory and starts a
  logging thread whether or not it ever logs. Calling `log::init` yourself REPLACES that logger
  rather than adding one — which is also how you flush it. `qb::ring_buffer<T,N>` is the window
  (note it publishes `capacity()`/`empty()`/`full()` but **no `size()`**), `qb::tsc_ticks()` the
  measurement, and `qb::CPU` the machine the numbers came from.
* **Run**: `./build/presets/release/examples/02-io/qb-example-io-logging-and-metrics`

---

## What this tier does not yet cover

All eleven programs of the design exist. What is still missing inside them is narrower and worth
naming: `connect_with_socket`'s coroutine form and `protocol::accept`/`protocol::handshake` (the
handshake protocol drives a TLS handshake explicitly and is only reached by qbm-http/2 today), the
`transport::udp::identity` demultiplexing that `04-udp` points at, and `async::epoll`, which is
Linux-only and deliberately not wired into the loop. Nothing here should be read as saying they do
not exist.
