# Example: Distributed File Processor (`file_processor`)

This example implements a manager-worker file processing system with the QB C++ Actor Framework: one dispatcher, a
pool of workers spread over several cores, and a client that drives a scripted test load.

## Overview

A `ClientActor` asks for files to be written and read back. It never touches a file itself — it sends events to a
`FileManager`, which hands each request to whichever `FileWorker` is free, queueing the request if none is.

**Key Components:**

* **`FileManager` Actor (`file_manager.h`)** — core 0.
    * Receives `ReadFileRequest` / `WriteFileRequest` from clients (`file_manager.h:93`, `:125`).
    * Fills in a missing `requestor` (from `getSource()`) and a missing `request_id` from its own counter.
    * If `_available_workers` is non-empty it pops one and forwards the request to it; otherwise it queues the request
      in `_read_requests` / `_write_requests`.
    * On `WorkerAvailable` it drains the queues — **reads first**, writes second — and only marks the worker idle when
      both queues are empty (`:157-181`).
* **`FileWorker` Actor (`file_worker.h`)** — four instances, on cores `1 + (i % 3)`, i.e. 1, 2, 3, 1.
    * Performs the actual `open`/`read`/`write`/`close` with `qb::io::sys::file`.
    * Pushes the `ReadFileResponse` / `WriteFileResponse` **straight to `request.requestor`** (`:140`, `:189`), then
      clears `_is_busy` and sends `WorkerAvailable` back to the manager.
* **`ClientActor` (`main.cpp`)** — core 0.
    * 500 ms after init, writes five files into `./test_files` with increasing sizes (`main.cpp:150-167`).
    * Each `WriteFileResponse` triggers a read of the file just written (`main.cpp:133`), so the run is 5 writes then
      5 reads.
    * When `_pending_requests` reaches 0 it waits 1 s and then `broadcast<qb::KillEvent>()` (`main.cpp:194-208`).
* **Shared Events (`messages.h`)** — `ReadFileRequest`, `ReadFileResponse`, `WriteFileRequest`, `WriteFileResponse`,
  `WorkerAvailable`.

### Two things the code does that the comments do not

* **The manager's response-forwarding handlers are dead as wired.** `FileManager` registers and implements
  `on(ReadFileResponse&)` / `on(WriteFileResponse&)` (`file_manager.h:187`, `:200`), but no one ever sends it one:
  the worker pushes the response directly to `request.requestor`, which is the `ClientActor` (`file_worker.h:150`,
  `:199`), because `ClientActor` already fills that field when it creates the request (`main.cpp:229`, `:240`). The
  forwarding path is there for a design where responses come back through the manager; this program does not use it.
* **"Offload blocking I/O" is not what happens.** Both workers wrap their file work in
  `qb::io::async::callback(lambda)` with **one** argument (`file_worker.h:112`, `:168`). That overload schedules
  nothing — it calls the function **inline, synchronously**, right there in the handler
  (`qb/src/qb/io/async/io.h:366-370`). The `open`/`read`/`write` therefore run on the worker's own VirtualCore thread,
  inside the event handler, exactly as if the lambda had been inlined. The parallelism in this example is real, but it
  comes from having **four worker actors on three cores**, not from the callback. To actually defer to the next loop
  turn you would use `qb::io::async::defer(fn)` (`qb/src/qb/io/async/listener.h:1030-1034`); to run after a delay,
  `qb::io::async::callback(fn, duration)`.

> **`ClientActor`'s two delays are coroutines, not timers — and that is the point.**
> `main.cpp:109` (`startTests` after 500 ms) and `main.cpp:252` (broadcast the kill after 1 s) used to be
> `qb::io::async::callback([this]..., delay)`, which heap-allocates a `Timeout` owned by the **event loop**, not by the
> actor (`qb/src/qb/io/async/io.h:389`). Nothing cancels it if the actor dies first, and the lambda then dereferences
> freed memory. Both now go through one lifetime-bound helper (`ClientActor::scheduleTick<T>`, `main.cpp:203-217`):
>
> ```cpp
> template <typename TickEvent>
> void scheduleTick(qb::duration d) {
>     spawn([d](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
>         co_await ctx.sleep(d);
>         ctx.template push<TickEvent>();
>     });
> }
> ```
>
> `spawn` is `qb/src/qb/core/Actor.h:1238-1239`; `Actor::kill()` cancels its scope at
> `qb/src/qb/core/Actor.cpp:283-289`. Never capture `this` in the coroutine — the work moves into
> `on(StartTestsTick&)` / `on(ShutdownTick&)`, which only run on a live actor. Note the contrast with the workers
> above: their `callback(fn)` is the one-argument overload and runs inline, so capturing `this` there is fine.

## QB Features Demonstrated

* **Core (`qb-core`)**:
    * `qb::Actor` for all three roles; `qb::Main` for engine setup and lifecycle.
    * Multi-core deployment with `engine.addActor<T>(core_id, ...)` (`main.cpp:269-286`).
    * Coroutine initialisation: `qb::io::async::task<bool> onInit()` with `co_return true`
      (`file_manager.h:83-87`, `file_worker.h:80-88`, `main.cpp:99-112`).
    * Custom `qb::Event` types carrying a `std::shared_ptr<std::vector<char>>` payload, so file contents cross cores
      without being copied into the event.
    * `push<Event>(dest, ...)` for every interaction; `broadcast<qb::KillEvent>()` for shutdown.
    * Manager-worker pattern with request queueing and a `std::unordered_set` of idle workers.
* **I/O (`qb-io`)**:
    * `qb::io::sys::file` — note the namespace is `qb::io::sys`, not `qb::io::system`
      (`qb/src/qb/io/system/file.h:44`).
    * `qb::io::async::callback` — see the caveat above; the one-argument form used here is synchronous.
    * Thread-safe console I/O: `qb::io::cout()`, `qb::io::cerr()`.

## How to Build and Run

1. **Build** — from the workspace root (the superproject force-enables `QB_BUILD_EXAMPLES`):
   ```bash
   cmake --preset dev
   cmake --build build/presets/dev --target file_processor
   ```
   The executable lands in `build/presets/dev/examples/core_io/file_processor/`.

2. **Run**:
   ```bash
   ./build/presets/dev/examples/core_io/file_processor
   ```
   The target directory is hard-coded to `./test_files`, relative to the **current working directory**
   (`main.cpp:216`); it is created if missing. There is no command-line argument.

   Expect, in order: the manager and four workers announcing their cores, five write requests dispatched, five write
   responses each followed by a read request, five read responses printing the first 50 bytes of each file, then
   `=== All tests completed ===` and a coordinated shutdown one second later.

   The program does **not** delete `./test_files` — remove it by hand if you care.
