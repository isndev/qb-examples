#!/usr/bin/env python3
"""Check bounded shutdown of the interactive examples on POSIX.

Uses the build roster, real loopback peers and an open stdin pipe. The old
std::cin.get() servers failed both the partial-line and EOF cases; a blocked
actor-core getline() failed the client SIGTERM cases. No full corpus is run.
"""

import argparse
import os
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path


MIN_CASES = 21
STARTUP_SECONDS = 8
EXIT_SECONDS = 6
TARGETS = (
    "qb-example-services-tcp-chat-server",
    "qb-example-services-tcp-chat-client",
    "qb-example-services-pubsub-broker-server",
    "qb-example-services-pubsub-broker-client",
    "qb-example-io-custom-protocol",
    "qb-example-modules-ws-chat-server",
    "qb-example-modules-ws-chat-client",
)


def binaries_from_roster(build_dir):
    roster = build_dir / "examples" / "example-roster.txt"
    if not roster.is_file():
        raise AssertionError(f"missing single-config build roster: {roster}")
    found = {}
    for line in roster.read_text(encoding="utf-8").splitlines():
        fields = line.split("|")
        if len(fields) != 4 or fields[0] != "built" or fields[1] not in TARGETS:
            continue
        target = fields[1]
        if target in found:
            raise AssertionError(f"duplicate {target} in {roster}")
        binary = Path(fields[3]).resolve()
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise AssertionError(f"roster binary missing or not executable: {binary}")
        found[target] = binary
    missing = set(TARGETS) - found.keys()
    if missing:
        raise AssertionError(f"interactive target(s) absent from roster: {sorted(missing)}")
    return found


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def preflight_port(port):
    if port_accepts(port):
        raise AssertionError(f"TCP port {port} already accepts connections")
    with socket.socket() as probe:
        try:
            # Repeat fixed-port cases despite TIME_WAIT from the preceding pair.
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(("0.0.0.0", port))
        except OSError as exc:
            raise AssertionError(f"TCP port {port} already occupied: {exc}") from exc


def port_accepts(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            return True
    except OSError:
        return False


def require_port_closed(port):
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        if not port_accepts(port):
            return
        time.sleep(0.05)
    raise AssertionError(f"TCP port {port} still accepts after server shutdown")


class Process:
    def __init__(self, binary, *args):
        self.directory = tempfile.TemporaryDirectory(prefix="qb-interactive-stdin-")
        self.log_path = Path(self.directory.name) / "process.log"
        self.log = self.log_path.open("w+b")
        try:
            self.proc = subprocess.Popen(
                [str(binary), *map(str, args)], stdin=subprocess.PIPE, stdout=self.log,
                stderr=subprocess.STDOUT, cwd=self.directory.name, start_new_session=True,
            )
        except Exception:
            self.log.close()
            self.directory.cleanup()
            raise

    def __enter__(self):
        return self

    def __exit__(self, _kind, _error, _traceback):
        if self.proc.poll() is None:
            self.term()
            try:
                self.proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                self.proc.wait(timeout=2)
        if self.proc.stdin and not self.proc.stdin.closed:
            try:
                self.proc.stdin.close()
            except OSError:
                pass
        self.log.close()
        self.directory.cleanup()

    def output(self):
        self.log.flush()
        return self.log_path.read_text(encoding="utf-8", errors="replace")

    def ready(self, marker, port=None):
        deadline = time.monotonic() + STARTUP_SECONDS
        while time.monotonic() < deadline:
            if marker in self.output() and (port is None or port_accepts(port)):
                return
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        raise AssertionError(
            f"{self.proc.args!r} never became ready ({marker!r}, port={port}), "
            f"rc={self.proc.poll()}\n{self.output()[-1500:]}"
        )

    def alive(self):
        if self.proc.poll() is not None:
            raise AssertionError(
                f"{self.proc.args!r} stopped early, rc={self.proc.returncode}\n"
                f"{self.output()[-1500:]}"
            )

    def write(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def term(self):
        if self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

    def exits_zero(self):
        try:
            rc = self.proc.wait(timeout=EXIT_SECONDS)
        except subprocess.TimeoutExpired as exc:
            raise AssertionError(
                f"{self.proc.args!r} did not stop within {EXIT_SECONDS}s\n"
                f"{self.output()[-1500:]}"
            ) from exc
        if rc != 0:
            raise AssertionError(f"{self.proc.args!r} exited {rc}\n{self.output()[-1500:]}")


def server_command(binaries, name, port):
    if name == "chat":
        return binaries["qb-example-services-tcp-chat-server"], (), "Engine is running"
    if name == "broker":
        return binaries["qb-example-services-pubsub-broker-server"], (), "Engine is running"
    return binaries["qb-example-io-custom-protocol"], ("server", str(port)), "Server running. Press Enter to stop."


def server_case(binaries, name, action):
    port = {"chat": 3001, "broker": 12345}[name] if name != "custom" else free_port()
    preflight_port(port)
    if name == "chat":
        preflight_port(3002)
    binary, args, marker = server_command(binaries, name, port)
    with Process(binary, *args) as server:
        server.ready(marker, port)
        if action == "enter":
            server.write(b"\n")
            server.exits_zero()
        elif action == "eof":
            server.proc.stdin.close()
            time.sleep(0.2)
            server.alive()
            if not port_accepts(port):
                raise AssertionError(f"{name} stopped listening after stdin EOF")
            server.term()
            server.exits_zero()
        else:
            server.write(b"unfinished")
            time.sleep(0.2)
            server.alive()
            server.term()
            server.exits_zero()
    require_port_closed(port)
    if name == "chat":
        require_port_closed(3002)


def client_case(binaries, name, action):
    port = 3001 if name == "chat" else 12345
    preflight_port(port)
    if name == "chat":
        preflight_port(3002)
    server_binary, server_args, marker = server_command(binaries, name, port)
    client_target = (
        "qb-example-services-tcp-chat-client" if name == "chat"
        else "qb-example-services-pubsub-broker-client"
    )
    client_binary = binaries[client_target]
    client_args = ("127.0.0.1", str(port), "runner") if name == "chat" else ("127.0.0.1", str(port))
    with Process(server_binary, *server_args) as server:
        server.ready(marker, port)
        with Process(client_binary, *client_args) as client:
            client.ready("Connected to server")
            if action == "quit":
                client.write(b"quit\n")
            elif action == "eof":
                client.proc.stdin.close()
            else:
                client.write(b"unfinished")
                time.sleep(0.2)
                client.alive()
                client.term()
            client.exits_zero()
        server.write(b"\n")
        server.exits_zero()
    require_port_closed(port)


def custom_client_case(binaries, action):
    port = free_port()
    preflight_port(port)
    binary = binaries["qb-example-io-custom-protocol"]
    with Process(binary, "server", str(port)) as server:
        server.ready("Server running. Press Enter to stop.", port)
        with Process(binary, "client", "127.0.0.1", str(port)) as client:
            client.ready("Connection protocol completed successfully")
            if action == "quit":
                client.write(b"quit\n")
                client.exits_zero()
                server.write(b"\n")
                server.exits_zero()
            elif action == "eof":
                client.proc.stdin.close()
                client.exits_zero()
                server.write(b"\n")
                server.exits_zero()
            else:
                client.write(b"unfinished")
                time.sleep(0.2)
                client.alive()
                server.write(b"\n")
                server.exits_zero()
                client.exits_zero()
    require_port_closed(port)


def websocket_client_case(binaries, action):
    port = free_port()
    preflight_port(port)
    server_binary = binaries["qb-example-modules-ws-chat-server"]
    client_binary = binaries["qb-example-modules-ws-chat-client"]
    with Process(server_binary, "--port", str(port)) as server:
        server.ready("Starting WebSocket Chat Server...", port)
        with Process(client_binary, f"ws://127.0.0.1:{port}/ws") as client:
            # A banner or TCP connect alone would pass without a WebSocket upgrade.
            client.ready("✓ Connected to WebSocket server!", port)
            if action == "quit":
                client.write(b"/quit\n")
            elif action == "eof":
                client.proc.stdin.close()
            else:
                client.write(b"unfinished")
                time.sleep(0.2)
                client.alive()
                client.term()
            client.exits_zero()
        server.term()
        server.exits_zero()
    require_port_closed(port)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "posix":
        raise SystemExit("POSIX signal/process-group check only; native Windows is validated separately")
    binaries = binaries_from_roster(args.build_dir.resolve())
    cases = [
        (f"{name} server {action}", lambda name=name, action=action: server_case(binaries, name, action))
        for name in ("chat", "broker", "custom")
        for action in ("sigterm-partial", "enter", "eof")
    ]
    cases += [
        (f"{name} client {action}", lambda name=name, action=action: client_case(binaries, name, action))
        for name in ("chat", "broker")
        for action in ("sigterm-partial", "quit", "eof")
    ]
    cases += [
        (f"custom client {action}", lambda action=action: custom_client_case(binaries, action))
        for action in ("disconnect-partial", "quit", "eof")
    ]
    cases += [
        (f"websocket client {action}", lambda action=action: websocket_client_case(binaries, action))
        for action in ("sigterm-partial", "quit", "eof")
    ]
    if len(cases) < MIN_CASES:
        raise AssertionError(f"registered {len(cases)} cases, floor {MIN_CASES}")
    executed = 0
    try:
        for label, check in cases:
            executed += 1
            check()
            print(f"PASS {label}", flush=True)
    finally:
        print(f"interactive stdin: {len(cases)} registered / {executed} executed / 0 skipped", flush=True)
    if executed < MIN_CASES:
        raise AssertionError(f"executed {executed} cases, floor {MIN_CASES}")


if __name__ == "__main__":
    main()
