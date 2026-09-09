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
