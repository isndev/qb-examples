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
# Tier 02 — qb-io, standalone
#
# The event loop, files, sockets and protocols with NO actor and NO qb::Main. This
# tier has no prerequisite on tier 01 and the tree proves it: there is no qb::Actor,
# no qb::Main and no addActor anywhere under this directory.
#
# Every target below is DERIVED from the directory and the source file by
# `qb_example` (examples/cmake/qbExample.cmake) — no target name is written here,
# and the binary carries the target's name because OUTPUT_NAME is never set:
#
#     02-io/01-event-loop.cpp      ->  qb-example-io-event-loop
#     02-io/02-files.cpp           ->  qb-example-io-files
#     02-io/03-tcp.cpp             ->  qb-example-io-tcp
#     02-io/04-udp.cpp             ->  qb-example-io-udp
#     02-io/05-custom-protocol.cpp ->  qb-example-io-custom-protocol
#
# DEPENDS is qb-io alone, not `qb-core qb-io`. The old CMakeLists linked qb-core into
# all five and none of them uses it — the tier's whole claim is that qb-io stands up
# without the engine, and a link line that drags the engine in anyway makes that claim
# untestable. If a file here ever needs qb-core, it belongs in another tier.
# -----------------------------------------------------------------------------

project(qb_examples_02_io
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Tier 02 — asynchronous I/O with qb-io, standalone"
)

qb_status_message("Building qb examples — tier 02-io")

qb_example(SOURCE 01-event-loop.cpp DEPENDS qb-io)

# Memory-mapped I/O and the POSIX stat()/mmap() half of this program have no Windows
# equivalent here, so the example is not created there rather than failing to compile.
if (NOT QB_PLATFORM_WINDOWS)
    qb_example(SOURCE 02-files.cpp DEPENDS qb-io)
endif ()

qb_example(SOURCE 03-tcp.cpp DEPENDS qb-io)
qb_example(SOURCE 04-udp.cpp DEPENDS qb-io)
qb_example(SOURCE 05-custom-protocol.cpp DEPENDS qb-io)
qb_example(SOURCE 06-framing-toolbox.cpp DEPENDS qb-io)

# 07-tls needs a certificate. `REQUIRES ssl` means the target is NOT CREATED in an SSL-off build
# (the roster records it as gated so the runner reports a SKIP with the capability named, rather
# than a program that has gone missing), and TARGET_VAR hands the derived name back so the resource
# staging below never has to retype it.
qb_example(SOURCE 07-tls.cpp DEPENDS qb-io REQUIRES ssl TARGET_VAR QB_EXAMPLE_IO_TLS)

qb_example(SOURCE 08-timeouts-and-watchers.cpp DEPENDS qb-io)
qb_example(SOURCE 09-graceful-drain.cpp DEPENDS qb-io)

qb_example(SOURCE 10-crypto-and-compression.cpp DEPENDS qb-io REQUIRES ssl compression)
qb_example(SOURCE 11-logging-and-metrics.cpp DEPENDS qb-io)

# Stage resources/ next to the tier's binaries so 07-tls opens "resources/ssl/cert.pem" with a
# plain relative path. `if (TARGET ...)` and not an unconditional call: in an SSL-off build the
# gate above returned before add_executable() and the target genuinely does not exist.
if (TARGET ${QB_EXAMPLE_IO_TLS})
    qb_stage_example_resources(
            STAGE_NAME qb-example-io-resources
            TARGETS ${QB_EXAMPLE_IO_TLS}
    )
endif ()
