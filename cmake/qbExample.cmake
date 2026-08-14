#
# qb - C++ Actor Framework
# Copyright (c) 2011-2025 qb - isndev (cpp.actor). All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# -----------------------------------------------------------------------------
# qb_example — the ONE place an example's CMake target name comes from.
#
# THE PROBLEM THIS SOLVES. An example carries four names: the tier DIRECTORY it
# lives in, its SOURCE file, its CMake TARGET, and the BINARY that comes out. Three
# of them used to be written by hand, in two different files, and nothing made them
# agree. They did not: `examples/03-coroutines/02-actor-coroutines.cpp` used to document
# `--target actor_coroutine_example`, a target that has never existed in this tree.
# That instance has since been repaired BY HAND, and the repair is the argument for
# this file rather than against it: the header now carries a parenthesis at :14
# explaining which name was wrong, which is what fixing an instance buys you. As long
# as a contributor can TYPE the target, the target can disagree with the file.
#
# So the last two layers are DERIVED and there is nowhere left to type them:
#
#     tier   := the tier directory's name, minus its NN- prefix       02-io -> io
#     slug   := the source file's basename, minus its NN- prefix and .cpp
#                                                       03-tcp.cpp -> tcp
#     target := qb-example-${tier}-${slug}                qb-example-io-tcp
#     binary := the target, because OUTPUT_NAME is never set and this wrapper
#               REFUSES the keyword (see the ARGN guard below).
#
# A tier's second level is one of exactly two things, told apart by whether it carries an
# NN- prefix, and they mean different things because the trees under them differ:
#
#     05-services/01-tcp-chat/     a PROJECT   — one program, many files, maybe many
#                                  binaries; the slug comes from the project directory
#                                  and ROLE tells its binaries apart
#     06-modules/http/             a GROUP     — many programs, one file each; the slug
#                                  still comes from the SOURCE, and the group name is
#                                  inserted so `qb-example-modules-http-routing` says
#                                  which module it teaches
#
# The group shape exists because tier 06 is four module surfaces (http · ws · pgsql ·
# redis) and flattening them would either collide slugs or renumber 36 files into an
# order nobody chose. It is NOT a general nesting rule: a group holds single-file
# examples only, takes no ROLE, and nothing goes below it.
#
# The derivation is total: every accepted input produces exactly one name, and every
# input this convention does not accept is a configure-time FATAL_ERROR naming what
# is wrong. A misnamed file therefore cannot build under a well-formed target — the
# failure mode that used to be silent is now the loudest thing in the configure log.
#
# WHY THE SELF-TEST AT THE BOTTOM OF THIS FILE. `_qb_example_derive` is a pure
# string function, so it can be tested with literal vectors and no filesystem — and
# it is, on every configure, in BOTH polarities. A derivation rule that only ever
# sees well-formed input is indistinguishable from one that accepts everything; the
# rejection vectors are what tell those two apart. This is the same discipline
# `dev/agent/*-negative-control.sh` applies to the doc guards, at the scale this
# file deserves.
#
# WHAT THIS WRAPPER DELIBERATELY DOES NOT DO. It does not stage resources. The
# directory-wide `qb_stage_example_resources()` in `examples/cmake/qbExampleResources.cmake`
# builds ONE copy target per example directory precisely so parallel builds do not race
# on shared files, and folding it in here would produce one per executable. It stays an
# explicit call — but the caller no longer has to retype a target name to make it, because
# `TARGET_VAR` hands the derived name back. Derivation is this file's job; staging is not.
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# _qb_example_derive(<out_target> <out_error> <dir_rel> <src_basename> <role>)
#
# Pure string derivation — no filesystem, no targets, no side effects, so it is
# testable with literal vectors (see the bottom of this file).
#
#   <dir_rel>  the example directory's path RELATIVE to examples/, one of
#              "02-io"                    a tier holding single-file examples
#              "05-services/01-tcp-chat"  a multi-file project inside a tier
#              "06-modules/http"          a module group inside a tier, holding
#                                         single-file examples of one module
#   <role>     "" for a single-file example; otherwise the role suffix that
#              distinguishes one binary of a project from its siblings.
#
# On success sets <out_target> to the derived name and <out_error> to "".
# On failure sets <out_target> to "" and <out_error> to a sentence saying what is
# wrong with which input — it never calls message(FATAL_ERROR) itself, because a
# function that aborts cannot be shown to reject anything.
# -----------------------------------------------------------------------------
function(_qb_example_derive out_target out_error dir_rel src_basename role)
    # Defined here, not at file scope: a subdirectory that shadowed or cleared a
    # directory-scoped variable would change what this function ACCEPTS, silently.
    # A tier directory: two digits, a hyphen, then a lowercase hyphenated slug. `02-io`.
    set(_tier_re "^([0-9][0-9])-([a-z0-9]+(-[a-z0-9]+)*)$")
    # A single-file example source: the same shape, plus .cpp. `03-tcp.cpp`.
    set(_src_re "^([0-9][0-9])-([a-z0-9]+(-[a-z0-9]+)*)\\.cpp$")
    # A role suffix telling one binary of a multi-binary project from its siblings.
    set(_role_re "^[a-z0-9]+(-[a-z0-9]+)*$")
    # A module group: the same slug shape with NO number, because a group is not a step in
    # a reading order -- it is which module the programs inside it are about.
    set(_group_re "^[a-z0-9]+(-[a-z0-9]+)*$")

    set(${out_target} "" PARENT_SCOPE)
    set(${out_error} "" PARENT_SCOPE)

    if ("${dir_rel}" STREQUAL "" OR "${dir_rel}" MATCHES "^/")
        set(${out_error} "example directory '${dir_rel}' is not inside examples/" PARENT_SCOPE)
        return()
    endif ()

    string(REPLACE "/" ";" _segs "${dir_rel}")
    list(LENGTH _segs _n)
    if (_n GREATER 2)
        set(${out_error}
            "example directory '${dir_rel}' is ${_n} levels below examples/; the tree is \
tier/ for single-file examples and tier/project/ for multi-file projects, nothing deeper"
            PARENT_SCOPE)
        return()
    endif ()

    list(GET _segs 0 _tier_seg)
    if (NOT "${_tier_seg}" MATCHES "${_tier_re}")
        set(${out_error}
            "tier directory '${_tier_seg}' does not match NN-<slug> (two digits, hyphen, \
lowercase hyphenated slug) — e.g. 02-io"
            PARENT_SCOPE)
        return()
    endif ()
    set(_tier "${CMAKE_MATCH_2}")

    if (_n EQUAL 1)
        # Single-file example: the slug comes from the source file.
        if (NOT "${role}" STREQUAL "")
            set(${out_error}
                "ROLE '${role}' was given for a single-file example; a role distinguishes the \
binaries of a multi-file project and has nothing to name here"
                PARENT_SCOPE)
            return()
        endif ()
        if (NOT "${src_basename}" MATCHES "${_src_re}")
            set(${out_error}
                "source '${src_basename}' does not match NN-<slug>.cpp (two digits, hyphen, \
lowercase hyphenated slug, no underscores) — e.g. 03-tcp.cpp"
                PARENT_SCOPE)
            return()
        endif ()
        set(_slug "${CMAKE_MATCH_2}")
    else ()
        list(GET _segs 1 _second_seg)
        if ("${_second_seg}" MATCHES "${_tier_re}")
            # Multi-file project: the slug comes from the project DIRECTORY, and each binary
            # inside it is told apart by its role. The source file names are free here — a
            # project has main.cpp, actors/*.cpp and so on, and numbering them would say
            # nothing about reading order.
            set(_slug "${CMAKE_MATCH_2}")
            if (NOT "${role}" STREQUAL "")
                if (NOT "${role}" MATCHES "${_role_re}")
                    set(${out_error}
                        "ROLE '${role}' must be a lowercase hyphenated slug — e.g. server"
                        PARENT_SCOPE)
                    return()
                endif ()
                set(_slug "${_slug}-${role}")
            endif ()
        elseif ("${_second_seg}" MATCHES "${_group_re}")
            # Module group: many single-file programs about ONE module. The slug still comes
            # from the source file, exactly as it does at tier level, and the group name is
            # inserted in front of it so the target says which module it is about.
            if (NOT "${role}" STREQUAL "")
                set(${out_error}
                    "ROLE '${role}' was given inside the module group '${_second_seg}'; a group \
holds one program per file, so there is no project whose binaries a role could tell apart"
                    PARENT_SCOPE)
                return()
            endif ()
            if (NOT "${src_basename}" MATCHES "${_src_re}")
                set(${out_error}
                    "source '${src_basename}' does not match NN-<slug>.cpp (two digits, hyphen, \
lowercase hyphenated slug, no underscores) — e.g. 02-routing.cpp"
                    PARENT_SCOPE)
                return()
            endif ()
            set(_slug "${_second_seg}-${CMAKE_MATCH_2}")
        else ()
            set(${out_error}
                "'${_second_seg}' is neither a project directory (NN-<slug>, e.g. 01-tcp-chat) \
nor a module group (a lowercase hyphenated slug with no number, e.g. http)"
                PARENT_SCOPE)
            return()
        endif ()
    endif ()

    set(${out_target} "qb-example-${_tier}-${_slug}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# qb_example(
#     SOURCE     <file.cpp> [<more.cpp> ...]   # first one names the target when single-file
#     [ROLE      <role>]                       # multi-file projects only
#     [DEPENDS   qb-core qb-io ...]
#     [REQUIRES  ssl|quic|compression]         # capability gate; unmet -> target NOT created
#     [DEFINES   ...] [INCLUDES ...]
#     [TARGET_VAR <var>])                      # derived target name, back to the caller
#
# Forwards to `qb_add_executable` (qb/cmake/qbFunctions.cmake:287), which owns the
# capability gate at :300-306. NAME and OUTPUT_NAME are refused, not forwarded: they
# are the two strings whose hand-writing this wrapper exists to abolish.
#
# TARGET_VAR is unset when a REQUIRES gate skipped the target, so `if(DEFINED)` /
# `if(TARGET ...)` at the call site behaves the same way it does for a directly gated
# `qb_add_executable` — a gated-out example does not exist, it does not fail.
# -----------------------------------------------------------------------------
function(qb_example)
    set(oneValueArgs ROLE TARGET_VAR)
    set(multiValueArgs SOURCE DEPENDS REQUIRES DEFINES INCLUDES)
    cmake_parse_arguments(QE "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT QE_SOURCE)
        message(FATAL_ERROR "qb_example: SOURCE is required")
    endif ()
    if (QE_UNPARSED_ARGUMENTS)
        # NAME and OUTPUT_NAME land here. Say why rather than "unknown argument": the whole
        # point of this wrapper is that those two strings are computed, and a contributor
        # reaching for them is reaching for the defect class it closed.
        message(FATAL_ERROR
                "qb_example: unexpected argument(s): ${QE_UNPARSED_ARGUMENTS}\n"
                "  NAME and OUTPUT_NAME are deliberately NOT accepted — the target and the "
                "binary are derived from the directory and the source file so the four naming "
                "layers cannot drift. Rename the file if the name is wrong.")
    endif ()

    if (NOT DEFINED QB_EXAMPLES_ROOT)
        message(FATAL_ERROR
                "qb_example: QB_EXAMPLES_ROOT is not set — it is set once by "
                "examples/CMakeLists.txt and is what makes a directory path derivable")
    endif ()

    file(RELATIVE_PATH _dir_rel "${QB_EXAMPLES_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}")
    list(GET QE_SOURCE 0 _primary)
    get_filename_component(_primary_name "${_primary}" NAME)

    _qb_example_derive(_target _err "${_dir_rel}" "${_primary_name}" "${QE_ROLE}")
    if (_err)
        message(FATAL_ERROR "qb_example: ${_err}\n  (in examples/${_dir_rel})")
    endif ()

    # CMake target names are ONE FLAT NAMESPACE across the whole superproject (~921
    # targets at a full build), so uniqueness is a property of the convention, not of
    # this directory. Assert it here: CMake's own duplicate-target error names the
    # second definition and not the convention that produced the collision.
    if (TARGET ${_target})
        message(FATAL_ERROR
                "qb_example: target ${_target} already exists — two examples derive the same "
                "name. Slugs must be unique within a tier.")
    endif ()

    foreach (_src IN LISTS QE_SOURCE)
        if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
            message(FATAL_ERROR "qb_example: no such source: examples/${_dir_rel}/${_src}")
        endif ()
    endforeach ()

    qb_add_executable(
            NAME ${_target}
            SOURCES ${QE_SOURCE}
            DEPENDS ${QE_DEPENDS}
            REQUIRES ${QE_REQUIRES}
            DEFINES ${QE_DEFINES}
            INCLUDES ${QE_INCLUDES}
    )

    # A REQUIRES gate that was not met returns from qb_add_executable BEFORE
    # add_executable(), so the target genuinely does not exist. Register and hand back
    # only what was really created — a roster that lists phantom targets is worse than none.
    #
    # ...but a gated-out example is not NOTHING, and recording only the survivors is what
    # made the SSL gate invisible for so long: in an SSL-off build five programs do not fail,
    # they cease to exist, and every count downstream drops for a reason nothing states. So
    # the two outcomes are recorded SEPARATELY and both are emitted (see the roster at the
    # bottom of examples/CMakeLists.txt). The runner can then tell "this program was gated
    # out by a capability this build does not have" — a reportable SKIP — from "this program
    # should be here and is not", which is a failure. Without the second list those two are
    # the same observation.
    if (TARGET ${_target})
        set_property(GLOBAL APPEND PROPERTY QB_EXAMPLE_TARGETS "${_target}")
        # `${_primary}` and not `${_primary_name}`: a project names its primary source with a
        # subdirectory (`server/main.cpp`), and the basename alone would record a path that
        # does not exist. This is what `dev/agent/run-examples.py` maps a binary back to, to
        # read the `@expect` lines it must see printed — so it has to be right.
        set_property(GLOBAL APPEND PROPERTY QB_EXAMPLE_SOURCES
                     "examples/${_dir_rel}/${_primary}")
        if (QE_TARGET_VAR)
            set(${QE_TARGET_VAR} "${_target}" PARENT_SCOPE)
        endif ()
    else ()
        # `.` for "no capability named" cannot happen here (a target with no REQUIRES is always
        # created), but the field is written unconditionally so every gated record has the same
        # arity and a reader never has to guess which column is missing.
        set(_req "${QE_REQUIRES}")
        if (NOT _req)
            set(_req ".")
        endif ()
        string(REPLACE ";" "+" _req "${_req}")
        set_property(GLOBAL APPEND PROPERTY QB_EXAMPLE_GATED
                     "${_target}|examples/${_dir_rel}/${_primary}|${_req}")
    endif ()
endfunction()

# -----------------------------------------------------------------------------
# Self-test — runs on every configure, in both polarities.
#
# Vectors, not prose. The accept cases pin the exact strings the convention promises;
# the reject cases are the reason this block exists at all, because a derivation that
# accepts everything passes every accept case. Each rejection also asserts the message
# is non-empty: a rule that rejects without saying why is a configure log nobody reads.
# -----------------------------------------------------------------------------
function(_qb_example_selftest)
    # `.` means "empty" in these vectors. A bare `||` would rely on CMake preserving an
    # empty list element through string(REPLACE), which is exactly the kind of assumption
    # a self-test must not itself rest on — and `.` is a legal value for no field here,
    # so the placeholder cannot be mistaken for real input.

    # accept: dir_rel | source | role | expected target
    set(_ok
            "02-io|01-event-loop.cpp|.|qb-example-io-event-loop"
            "02-io|03-tcp.cpp|.|qb-example-io-tcp"
            "03-coroutines|04-ask-request-response.cpp|.|qb-example-coroutines-ask-request-response"
            "05-services/01-tcp-chat|main.cpp|server|qb-example-services-tcp-chat-server"
            "05-services/01-tcp-chat|main.cpp|client|qb-example-services-tcp-chat-client"
            "07-applications/03-market-data-hub|main.cpp|.|qb-example-applications-market-data-hub"
            # Module groups. The last pair is the one worth having: two modules with the
            # SAME slug must still derive different targets, which is the whole reason the
            # group name is in the string rather than only in the path.
            "06-modules/http|02-routing.cpp|.|qb-example-modules-http-routing"
            "06-modules/pgsql|03-transactions.cpp|.|qb-example-modules-pgsql-transactions"
            "06-modules/redis|05-transactions.cpp|.|qb-example-modules-redis-transactions"
            "06-modules/ws|01-chat-server.cpp|.|qb-example-modules-ws-chat-server")
    foreach (_v IN LISTS _ok)
        string(REPLACE "|" ";" _f "${_v}")
        list(GET _f 0 _d)
        list(GET _f 1 _s)
        list(GET _f 2 _r)
        list(GET _f 3 _want)
        if (_d STREQUAL ".")
            set(_d "")
        endif ()
        if (_r STREQUAL ".")
            set(_r "")
        endif ()
        _qb_example_derive(_got _err "${_d}" "${_s}" "${_r}")
        if (_err)
            message(FATAL_ERROR "qb_example self-test: '${_v}' should derive, but: ${_err}")
        endif ()
        if (NOT _got STREQUAL _want)
            message(FATAL_ERROR "qb_example self-test: '${_v}' derived ${_got}, expected ${_want}")
        endif ()
    endforeach ()

    # reject: dir_rel | source | role | what the convention refuses
    set(_bad
            "io|03-tcp.cpp|.|tier directory carries no NN- prefix"
            "2-io|03-tcp.cpp|.|tier number is one digit"
            "02-IO|03-tcp.cpp|.|tier slug is not lowercase"
            "02-io|tcp.cpp|.|source carries no NN- prefix"
            "02-io|03_tcp.cpp|.|source uses an underscore"
            "02-io|03-Tcp.cpp|.|source slug is not lowercase"
            "02-io|03-tcp.cc|.|source is not .cpp"
            "02-io|03-tcp.cpp|server|ROLE on a single-file example"
            "02-io/a/b|main.cpp|.|three levels below examples/"
            "05-services/01-tcp-chat|main.cpp|Server|role is not lowercase"
            ".|03-tcp.cpp|.|empty directory"
            # Module groups. `05-services/tcp-chat|main.cpp|server` is NOT here any more: a
            # second segment with no number is now a legal GROUP, so that vector stopped
            # testing "project directory carries no NN- prefix" and started testing whether a
            # group accepts a ROLE. It is spelled that way below, which is what it now means.
            "05-services/tcp-chat|main.cpp|server|ROLE inside a module group"
            "06-modules/http|hello-server.cpp|.|group source carries no NN- prefix"
            "06-modules/http|02_routing.cpp|.|group source uses an underscore"
            "06-modules/HTTP|02-routing.cpp|.|group name is not lowercase"
            "06-modules/http|02-routing.cc|.|group source is not .cpp")
    foreach (_v IN LISTS _bad)
        string(REPLACE "|" ";" _f "${_v}")
        list(GET _f 0 _d)
        list(GET _f 1 _s)
        list(GET _f 2 _r)
        list(GET _f 3 _why)
        if (_d STREQUAL ".")
            set(_d "")
        endif ()
        if (_r STREQUAL ".")
            set(_r "")
        endif ()
        _qb_example_derive(_got _err "${_d}" "${_s}" "${_r}")
        if (NOT _err)
            message(FATAL_ERROR
                    "qb_example self-test: '${_v}' (${_why}) was ACCEPTED and derived ${_got} — "
                    "the convention is not being enforced")
        endif ()
        if (NOT _got STREQUAL "")
            message(FATAL_ERROR
                    "qb_example self-test: '${_v}' (${_why}) was rejected but still handed back "
                    "the target '${_got}'")
        endif ()
    endforeach ()

    list(LENGTH _ok _n_ok)
    list(LENGTH _bad _n_bad)
    qb_debug_message("qb_example self-test: ${_n_ok} derivations, ${_n_bad} rejections")
endfunction()

_qb_example_selftest()
