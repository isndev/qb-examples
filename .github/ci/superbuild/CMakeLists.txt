# examples/.github/ci/superbuild/CMakeLists.txt — the minimal root that builds this corpus
# in CI (Huly QB-7).
#
# WHY A DRIVER AT ALL. This repository cannot be configured standalone: every example links
# qb targets, and tier 06 links the three qbm modules — none of which an examples checkout
# contains. The private superproject assembles them; a public contributor's PR had NOTHING
# (this repo shipped no workflow at all, so 99 programs protected no push to the repo that
# owns them — the same gap the three qbm modules closed in 3.0 with exactly this shape).
#
# The assembly reproduces the superproject root's three load-bearing lines:
#     add_subdirectory(qb)  ->  qb_load_modules(qbm/)  ->  add_subdirectory(examples)
# with the same cache posture (examples FORCE-ON, tests OFF — this lane builds the corpus,
# not qb's suite; qb's own CI owns that) and the same LANGUAGES C CXX lesson: the private
# root enables both project-wide, so configuring with CXX only would accept code the
# superproject rejects (AGENTS.md records the reverse of that bite).
#
# Usage (what tests.yml does):
#     cmake -S examples/.github/ci/superbuild -B build \
#           -DQB_EXAMPLES_CI_QB_DIR="$PWD/qb" \
#           -DQB_EXAMPLES_CI_QBM_DIR="$PWD/qbm" \
#           -DQB_EXAMPLES_CI_EXAMPLES_DIR="$PWD/examples"
cmake_minimum_required(VERSION 3.25)
project(qb-examples-ci LANGUAGES C CXX)

foreach(_ex_ci_var IN ITEMS QB_EXAMPLES_CI_QB_DIR QB_EXAMPLES_CI_QBM_DIR QB_EXAMPLES_CI_EXAMPLES_DIR)
    if(NOT DEFINED ${_ex_ci_var} OR NOT IS_DIRECTORY "${${_ex_ci_var}}")
        message(FATAL_ERROR
            "${_ex_ci_var} must point at an existing directory. This driver needs:\n"
            " -DQB_EXAMPLES_CI_QB_DIR=<an isndev/qb checkout>\n"
            " -DQB_EXAMPLES_CI_QBM_DIR=<a directory holding http/ pgsql/ redis/ module checkouts>\n"
            " -DQB_EXAMPLES_CI_EXAMPLES_DIR=<this repository's checkout>")
    endif()
endforeach()

# The corpus's module tier is a third of the corpus; a qbm dir missing one module would
# silently shrink the build to something the roster floor below then catches — but catch it
# HERE, by name, where the fix is obvious.
foreach(_ex_ci_mod IN ITEMS http pgsql redis)
    if(NOT IS_DIRECTORY "${QB_EXAMPLES_CI_QBM_DIR}/${_ex_ci_mod}")
        message(FATAL_ERROR
            "${QB_EXAMPLES_CI_QBM_DIR}/${_ex_ci_mod} is missing — tier 06-modules/${_ex_ci_mod} "
            "cannot build. The workflow checks out the three qbm modules into that layout.")
    endif()
endforeach()

set(QB_BUILD_TESTS OFF CACHE BOOL "qb tests are qb's own CI's job" FORCE)
set(QB_BUILD_EXAMPLES ON CACHE BOOL "the whole point of this lane" FORCE)
set(QB_BUILD_BENCHMARKS OFF CACHE BOOL "not here" FORCE)
set(QB_INSTALL OFF CACHE BOOL "a build lane, not a package lane" FORCE)

add_subdirectory("${QB_EXAMPLES_CI_QB_DIR}" "${CMAKE_BINARY_DIR}/qb")

if(NOT COMMAND qb_load_modules)
    message(FATAL_ERROR
        "the qb checkout at ${QB_EXAMPLES_CI_QB_DIR} defines no qb_load_modules(). This root "
        "reproduces the superproject's assembly; a qb without that function is older than the "
        "corpus it is asked to build.")
endif()
qb_load_modules("${QB_EXAMPLES_CI_QBM_DIR}")

add_subdirectory("${QB_EXAMPLES_CI_EXAMPLES_DIR}" "${CMAKE_BINARY_DIR}/examples")
