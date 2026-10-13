# `.github/ci/` — how this repository builds its corpus in CI

`isndev/qb-examples` ships [`.github/workflows/build.yml`](../workflows/build.yml). It assembles
qb, the three qbm repositories and this repository, then builds the corpus through the superbuild
driver in this directory. This page records why that shape is necessary, what the lane proves and
what still belongs to the private superproject's runtime gate.

## 1. This tree cannot be configured on its own — and it fails *quietly*

`CMakeLists.txt` here calls `qb_status_message()`, `qb_add_executable()` and
`qb_stage_example_resources()`. Those are defined only by **qb**, in `qb/cmake/qbFunctions.cmake`,
and an *installed* qb does not ship them (it installs `qbConfig`, `qbConfigVersion`, `qbTargets`
and the `Find*.cmake` modules — no `qb_*` commands). There is no `.gitmodules` here, no `qb/`, no
`qbm/`.

The dangerous part is the failure mode. `examples/CMakeLists.txt` opens with

```cmake
if (NOT QB_BUILD_EXAMPLES)
    return()
endif ()
```

so a standalone `cmake -S . -B build` does not error — it **returns before adding a single target
and exits 0 having generated nothing.** A naive workflow here would be green and completely
vacuous. Any lane that is ever added must therefore assert a floor on *targets created* and on
*translation units compiled*, not merely on `cmake` and `cmake --build` exiting 0.

## 2. The public lane assembles five repositories

It is not impossible, and it should not be described that way. What this tree needs is not an
installed qb but a **root that `add_subdirectory()`s a qb source tree first** — the same insight
that let `isndev/qbm-{http,pgsql,redis}` start gating their own pull requests (each of them carries
`.github/ci/superbuild/CMakeLists.txt` for exactly that).

Unlike a qbm module's two-checkout lane, these examples need **five** repositories because
`examples/CMakeLists.txt`
`add_subdirectory()`s `qbm/redis`, `qbm/pgsql`, `qbm/http` and `qbm/ws`, so a driver root would
have to assemble

```
<workspace>/qb          isndev/qb
<workspace>/qbm/http    isndev/qbm-http
<workspace>/qbm/pgsql   isndev/qbm-pgsql
<workspace>/qbm/redis   isndev/qbm-redis
<workspace>/examples    this repository
```

The workflow selects the same-named qb/qbm branch as the examples change (or an explicit manual
ref), refuses a missing companion ref, and feeds those five paths to
`.github/ci/superbuild/CMakeLists.txt`.

## 3. What the public lane proves

On pushes, pull requests, nightly runs and manual dispatches, `build.yml`:

1. configures the five-repository superbuild with pinned Clang;
2. builds the corpus and requires a non-vacuous translation-unit floor;
3. checks the generated roster's built/gated floor and every built binary's existence;
4. permits only the exact expected QUIC and HTTP/3 gated rows on the hosted runner, measured as
   97 built + 2 gated, and rejects a planted false QUIC row with the same predicate;
5. smoke-runs one named self-contained example; and
6. runs the PostgreSQL ownership checks in a disposable cluster.

It does not replace `dev/agent/run-examples.py` in the private **`qb-dev`** superproject. That
runner judges every program's ending and `@expect` output, probes live services, and verifies
server second-instance behavior. Before opening a change, run the relevant program through a
qb-dev checkout as well as relying on this repository's public build lane.
