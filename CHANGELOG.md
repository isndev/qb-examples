# Changelog

All notable changes to qb-examples are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the corpus versions in
lockstep with the qb train (see the release policy in the qb-dev superproject's AGENTS.md):
an entry belongs here only when it changes what a USER of these examples sees — a program's
behaviour, its exit contract, a lesson's content. Build scripts, CI and the runner's own
plumbing do not qualify.

## [Unreleased]

### Fixed

- **Two programs whose ending was decided by one core's timeline — the corpus runner's
  two signals, a hang and a dead path, each seen once on the arm64 CI runner.**
  `02-io/03-tcp` gated its shutdown command on a flag the SERVER thread clears when it sees
  the client's socket close — the client object's destructor, microseconds before
  `join()` returns — so the server's 10 ms tick could win and no shutdown was ever sent: a
  hang after the full script had printed, won ~999 times in 1 000 on x86 and lost 2 of 16
  on the arm64 VM; `main` decides on the server's own state now. `04-patterns/01-pubsub`
  stopped the engine on the survivor's second wave alone while the polite desk's exit ran on
  the other core with nothing ordering the two, so a run could print `=== pub/sub complete
  ===`, exit 0, and never print `[bus@1] a POLITE subscriber reclaims its slot at once`;
  each feed now reports to the reporter once its bus has judged the departed desk, and the
  run ends only when both buses have spoken AND the survivor's wave has landed (40/40 quiet,
  40/40 under twelve busy neighbours on WSL2). The lessons are untouched; what changed is
  that both programs now end for a reason every core agrees on.

## [3.1.0] - 2026-08-30

### Changed

- **This repository gets its own CI (Huly QB-7).** Until then 99 programs protected no push to the
  repository that owns them. A lane checks out the same-named qb and qbm branches, drives the
  superproject's superbuild, and holds floors that refuse vacuity: at least 90 translation units
  compiled, a roster of at least 99 built rows, every built binary present, one self-contained
  program run — with clang pinned like every other lane, and the roster floor set to what
  `ubuntu-latest` can build.
- **The two tier-7 post-SIGTERM `@expect` tails lose their platform tag**
  (`07-applications/01-taskmanager`, `07-applications/02-auction-house`): qb 3.1.0's console-control
  bridge delivers CTRL_BREAK as the SIGTERM their teardown already handles, so the lines the tag
  excluded on Windows are assertable there. The platform-tagged count returns to 2, both honest
  bounds (SIGHUP's existence, AFD's send buffer), neither a framework gap.

## [3.0.1] - 2026-08-29

### Fixed

- **A failed bind no longer reports success — three programs, one defect class.**
  `02-io/05-custom-protocol` ran its server and client through `void` helpers whose failure
  `return;` never reached `main`, which returned 0 unconditionally: a server that could not
  bind printed the error and exited clean. Both helpers now return `main`'s exit code — a
  held port is exit 1, like the other fifteen servers in this corpus. The two
  `05-services` servers (`01-tcp-chat`, `02-pubsub-broker`) had the deeper form: their
  acceptor's async `onInit` correctly `co_return false`s and the framework aborts every
  core at the init barrier — but `main` never consulted `engine.hasError()`, printed
  `Engine is running` over cores that had already exited, and sat waiting on stdin. Both
  mains now gate on `hasError()` — which `start(true)` makes answerable the moment it
  returns — and exit 1 with a message. Teaching programs were teaching exactly the failure
  mode a supervisor cannot see.

## [3.0.0] - 2026-08-20

The corpus as released with qb 3.0.0: 99 programs across 7 tiers, every one carrying a
verified header contract (`@expect`/`@demonstrates`) and run — not just built — by the
superproject's example runner on macOS, Linux and Windows.

### The pre-3.0 holding directories, and what became of them

Every tier is converted, and the five pre-3.0 holding directories (`core/`, `core_io/`, `qbm/`,
`coroutine/`, `all/`) have been **retired**. Every program in this tree now derives its CMake
target and its binary name from its path; there is no second naming convention left anywhere.

A retirement only lands *with* its replacement — never before it, or the corpus promises
something no file delivers. Each was checked one program at a time against the replacement's
CODE, not against the claim in its `CMakeLists.txt`:

| Retired | Replaced by |
|---|---|
| `core/example6_shared_queue.cpp` | `01-actors/03-event-payloads` — the same foreign-thread bridge, with a lock-free spsc ring in place of a mutex-guarded queue |
| `core/example7_pub_sub.cpp` | `04-patterns/01-pubsub` for the shipped `qb::PubSub<Topic>` bus, and `05-services/02-pubsub-broker` for runtime *topic-keyed* routing, which the bus does not do |
| `core/example9_trading_system.cpp` | `07-applications/03-market-data-hub` |
| `core/example10_distributed_computing.cpp` | `04-patterns/03-worker-pool` + `04-patterns/04-scatter-gather` + `02-io/11-logging-and-metrics` |
| `core_io/file_monitor/` | `02-io/08-timeouts-and-watchers` + `01-actors/03-event-payloads` + `05-services/03-file-pipeline` |
| `qbm/http/06_async_handlers.cpp` | `06-modules/http/04-middleware` + `06-modules/http/09-coroutine-handlers` |
| `qbm/redis/example2_hash_operations.cpp`, `example3_list_operations.cpp` | merged into `06-modules/redis/02-data-types` |
| `qbm/redis/example8_complex_actor_system.cpp` | `06-modules/redis/07-scripting` + `10-cache-actor` |

**Three lessons had no home, and were given one rather than used as a reason to keep a
superseded file alive.** `HKEYS`/`HVALS`/`LINDEX`/`LSET`/`BLPOP` survived only in prose
describing the two merged programs, so they went into `06-modules/redis/02-data-types`, which
*is* the merge. A plain cursor-based `XREAD` — a stream read with no consumer group — existed
nowhere, so it went into `06-modules/redis/06-streams`. And "relocatable is not owned" (boxing a
`shared_ptr` into an event settles whether the EVENT can be memcpy'd and says nothing about who
may write through the POINTEE) went into `01-actors/03-event-payloads`, beside the rule it is
the second half of.

[Unreleased]: https://github.com/isndev/qb-examples/compare/v3.1.0...HEAD
[3.1.0]: https://github.com/isndev/qb-examples/compare/v3.0.1...v3.1.0
[3.0.1]: https://github.com/isndev/qb-examples/compare/v3.0.0...v3.0.1
[3.0.0]: https://github.com/isndev/qb-examples/releases/tag/v3.0.0
