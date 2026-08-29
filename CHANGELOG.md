# Changelog

All notable changes to qb-examples are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the corpus versions in
lockstep with the qb train (see the release policy in the qb-dev superproject's AGENTS.md):
an entry belongs here only when it changes what a USER of these examples sees — a program's
behaviour, its exit contract, a lesson's content. Build scripts, CI and the runner's own
plumbing do not qualify.

## [Unreleased]

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
