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
# qb_stage_example_resources
#
# Stage an example directory's resources/ tree right NEXT TO the built executables,
# so the example C++ can load assets with a plain, unadorned "resources/..." relative
# path — no baked absolute paths, no runtime resolver loops, no shelling out to tools.
# The example sources stay pristine qb presentation code; every path mechanic lives here.
#
#   qb_stage_example_resources(
#       STAGE_NAME     <unique-custom-target-name>   # the ALL target that copies assets
#       TARGETS        <tgt> [<tgt> ...]             # example targets that read the assets
#       [RESOURCES_DIR <dir>])                       # default ${CMAKE_CURRENT_SOURCE_DIR}/resources
#
# Why a plain relative path is enough — the binary and its assets are GUARANTEED to sit
# in the same folder, on every generator, configuration and platform:
#
#   * Each target's runtime output is pinned to this directory's binary dir for EVERY
#     configuration. That makes the layout immune to:
#       - multi-config generators (Visual Studio, Xcode), which would otherwise drop the
#         binary in a Debug/ or Release/ subdir away from the staged resources, and
#       - a project-wide CMAKE_RUNTIME_OUTPUT_DIRECTORY (a very common "collect all
#         binaries in bin/" setting) that would otherwise divert the binary from its assets.
#   * The resources/ tree is copied ONCE by a single ALL target — no per-target copies
#     racing on the same files during a parallel build.
#   * VS_DEBUGGER_WORKING_DIRECTORY points the Visual Studio / IDE debugger at that folder,
#     so F5 / double-click / `cd` + run all resolve the assets with zero setup.
#
# On Linux/macOS, launch the binary from its own directory (it sits next to resources/) —
# that single step is unavoidable with relative paths, because VS_DEBUGGER_WORKING_DIRECTORY
# is a Visual-Studio-only property that Ninja/Make/CLion/gdb/lldb ignore.
# -----------------------------------------------------------------------------
function(qb_stage_example_resources)
    cmake_parse_arguments(QSER "" "STAGE_NAME;RESOURCES_DIR" "TARGETS" ${ARGN})

    if (NOT QSER_STAGE_NAME)
        message(FATAL_ERROR "qb_stage_example_resources: STAGE_NAME is required")
    endif ()
    if (NOT QSER_TARGETS)
        message(FATAL_ERROR "qb_stage_example_resources: at least one TARGET is required")
    endif ()
    if (NOT QSER_RESOURCES_DIR)
        set(QSER_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    endif ()
    if (NOT IS_DIRECTORY "${QSER_RESOURCES_DIR}")
        message(FATAL_ERROR
                "qb_stage_example_resources: resources directory not found: ${QSER_RESOURCES_DIR}")
    endif ()

    set(_stage_dir "${CMAKE_CURRENT_BINARY_DIR}")

    # Declare the staged files so they are NODES IN THE BUILD GRAPH, not just bytes some
    # command happened to write. Without BYPRODUCTS -- which appeared zero times in this tree
    # until 3.0 -- `ninja -t clean` leaves every staged asset behind, so a resource deleted or
    # renamed in the source tree survives a clean and keeps satisfying an example that should
    # by then be failing to find it. It also means nothing can express a dependency on the
    # FILE; ordering rests entirely on the add_dependencies() edge below.
    #
    # The glob is CONFIGURE_DEPENDS so adding an asset re-runs CMake: a stale declared list is
    # the same quiet drift in a new place. It cannot make the copy incremental -- a custom
    # TARGET is always out of date -- and that is deliberate, because converting one writer of
    # a path to add_custom_command(OUTPUT ...) while another stays a bare target makes ninja
    # think the file is up to date while the other writer overwrites it every build.
    file(GLOB_RECURSE _qser_files LIST_DIRECTORIES false RELATIVE "${QSER_RESOURCES_DIR}"
         CONFIGURE_DEPENDS "${QSER_RESOURCES_DIR}/*")
    set(_qser_byproducts "")
    foreach (_qser_f IN LISTS _qser_files)
        list(APPEND _qser_byproducts "${_stage_dir}/resources/${_qser_f}")
    endforeach ()

    # One copy for the whole example directory — no per-target race on shared files.
    add_custom_target(${QSER_STAGE_NAME} ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QSER_RESOURCES_DIR}" "${_stage_dir}/resources"
            BYPRODUCTS ${_qser_byproducts}
            COMMENT "Staging example resources next to the executables (${QSER_STAGE_NAME})"
            VERBATIM)

    foreach (_tgt IN LISTS QSER_TARGETS)
        if (NOT TARGET ${_tgt})
            message(FATAL_ERROR "qb_stage_example_resources: no such target '${_tgt}'")
        endif ()

        add_dependencies(${_tgt} ${QSER_STAGE_NAME})

        # Pin the binary next to the staged resources for the single-config default and
        # for every multi-config configuration (which would otherwise append /<Config>).
        set_target_properties(${_tgt} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${_stage_dir}"
                VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_tgt}>")
        foreach (_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
            string(TOUPPER "${_cfg}" _CFG)
            set_target_properties(${_tgt} PROPERTIES
                    RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${_stage_dir}")
        endforeach ()
    endforeach ()
endfunction()
