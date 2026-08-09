# `.github/ci/` — why this repository has no CI of its own

`isndev/qb-examples` ships **no workflow**. That is a deliberate, recorded state, not an oversight,
and this file exists so the next person does not rediscover the reasons from scratch.

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

## 2. A self-build lane *is* possible — it just needs five repositories

It is not impossible, and it should not be described that way. What this tree needs is not an
installed qb but a **root that `add_subdirectory()`s a qb source tree first** — the same insight
that let `isndev/qbm-{http,pgsql,redis}` start gating their own pull requests (each of them carries
`.github/ci/superbuild/CMakeLists.txt` for exactly that).

The difference in cost is the reason it has not been done here. A qbm module needs **two**
checkouts, qb and itself. These examples need **five**: `examples/CMakeLists.txt`
`add_subdirectory()`s `qbm/redis`, `qbm/pgsql`, `qbm/http` and `qbm/ws`, so a driver root would
have to assemble

```
<workspace>/qb          isndev/qb
<workspace>/qbm/http    isndev/qbm-http
<workspace>/qbm/pgsql   isndev/qbm-pgsql
<workspace>/qbm/redis   isndev/qbm-redis
<workspace>/examples    this repository
```

and then be, essentially, the private `qb-dev` superproject root with `QB_BUILD_EXAMPLES=ON`. Five
refs to keep in step, against a tree whose content is demonstrations rather than tests, is a
materially different proposition from the two-repo module lanes — so the coverage stays where it
already is (see below) until someone decides that trade is worth making. If you are that person:
copy the shape from `isndev/qbm-http`'s `.github/ci/superbuild/CMakeLists.txt` and
`.github/workflows/tests.yml`, add the three module checkouts, and keep the target/TU floors.

## 3. Where these examples *are* built today

In the private **`qb-dev`** superproject, by `.github/workflows/examples-build.yml`, on every push
and pull request. It builds the whole tree with both targeted toolchains and asserts non-vacuous
floors — measured at 3.0.0 from a cold build directory: **135 translation units, 55 executables,
0 warnings** — plus that SSL is genuinely on, because seven examples declare
`qb_add_executable(... REQUIRES ssl)` and are **not created at all** in an SSL-off build rather
than failing to compile.

What that does **not** do is gate a pull request opened *here*: `qb-dev` is private, so its result
arrives when a maintainer bumps this repository's submodule pointer, not when your PR is opened.
Until section 2 is acted on, please build your change against a qb-dev checkout (or the five-repo
assembly above) before opening it.
