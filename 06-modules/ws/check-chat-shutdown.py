#!/usr/bin/env python3
"""Exercise chat shutdown with live WebSocket sessions on a loopback port.

The ordinary example runner keeps the server alive but does not stop it while
an announced client is connected. That order used to recurse from a session
destructor and crash. Run this script with a built superproject roster.
"""

import argparse
import base64
import json
import os
import signal
import socket
import subprocess
import threading
import time
from pathlib import Path


TARGET = "qb-example-modules-ws-chat-server"


def find_binary(build_dir):
    roster = Path(build_dir) / "examples" / "example-roster.txt"
    matches = [line.split("|") for line in roster.read_text().splitlines()
               if line.startswith(f"built|{TARGET}|")]
    if len(matches) != 1 or len(matches[0]) != 4:
        raise AssertionError(f"expected exactly one built {TARGET} in {roster}")
    binary = Path(matches[0][3])
    if not binary.is_file():
        raise AssertionError(f"missing built server: {binary}")
    return binary


class Server:
    def __init__(self, binary):
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            self.port = reserved.getsockname()[1]
        self.proc = subprocess.Popen(
            [str(binary), "--port", str(self.port)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.lines = []
        self.cv = threading.Condition()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.clients = []

    def _read(self):
        for line in self.proc.stdout:
            with self.cv:
                if len(self.lines) < 2000:
                    self.lines.append(line.rstrip())
                self.cv.notify_all()

    def wait_line(self, text, timeout=5):
        deadline = time.monotonic() + timeout
        with self.cv:
            while not any(text in line for line in self.lines):
                remaining = deadline - time.monotonic()
                if remaining <= 0 or self.proc.poll() is not None:
                    return False
                self.cv.wait(min(remaining, 0.2))
            return True

    def connect(self, username=None):
        peer = socket.create_connection(("127.0.0.1", self.port), timeout=3)
        self.clients.append(peer)
        key = base64.b64encode(os.urandom(16)).decode()
        request = (f"GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\n"
                   f"Upgrade: websocket\r\nConnection: Upgrade\r\n"
                   f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
        peer.sendall(request.encode())
        response = b""
        while b"\r\n\r\n" not in response and len(response) < 8192:
            chunk = peer.recv(4096)
            if not chunk:
                break
            response += chunk
        if b" 101 " not in response.split(b"\r\n", 1)[0]:
            raise AssertionError(f"upgrade failed: {response[:160]!r}")

        if username is not None:
            payload = json.dumps({"type": "user_joined", "username": username}).encode()
            if len(payload) >= 126:
                raise AssertionError("test frame unexpectedly needs extended length")
            mask = os.urandom(4)
            frame = bytes((0x81, 0x80 | len(payload))) + mask
            frame += bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
            peer.sendall(frame)
            if not self.wait_line(f"[JOIN] {username} joined the chat"):
                raise AssertionError(f"server did not register {username}")
        return peer

    def stop(self):
        self.proc.send_signal(signal.SIGTERM)
        try:
            code = self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=3)
            raise AssertionError("chat server did not stop within five seconds")
        self.reader.join(timeout=1)
        if self.reader.is_alive():
            raise AssertionError("chat output was not fully read after server exit")
        return code

    def close(self):
        for peer in self.clients:
            peer.close()
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=3)
        self.reader.join(timeout=1)

    def count(self, prefix):
        with self.cv:
            return sum(line.startswith(prefix) for line in self.lines)

    def tail(self):
        with self.cv:
            return "\n".join(self.lines[-20:])


def check_case(binary, announced, normal_close=False, unannounced_close=False):
    server = Server(binary)
    label = f"announced={announced} normal_close={normal_close} unannounced_close={unannounced_close}"
    try:
        # Printed after Main::start(); the example flushes this readiness line.
        if not server.wait_line("WebSocket endpoint:"):
            raise AssertionError("server never reached startup readiness")
        for index in range(announced):
            server.connect(f"audit{index}")
        if unannounced_close:
            peer = server.connect()
            peer.shutdown(socket.SHUT_RDWR)
            peer.close()
            server.clients.remove(peer)
            if not server.wait_line("User disconnected. Total WebSocket users: 0"):
                raise AssertionError("unannounced session was not counted down")
        if normal_close:
            peer = server.clients.pop(0)
            peer.shutdown(socket.SHUT_RDWR)
            peer.close()
            if not server.wait_line("[LEAVE] audit0 left the chat"):
                raise AssertionError("ordinary disconnect did not announce one departure")
            if not server.wait_line("User disconnected. Total WebSocket users: 1"):
                raise AssertionError("ordinary disconnect did not update the user count")
        code = server.stop()
        expected_leaves = 1 if normal_close else 0
        if code != 0 or server.count("[JOIN]") != announced or server.count("[LEAVE]") != expected_leaves:
            raise AssertionError(f"exit={code}, joins={server.count('[JOIN]')}, "
                                 f"leaves={server.count('[LEAVE]')}, expected leaves={expected_leaves}")
        print(f"PASS {label}: exit=0, joins={announced}, leaves={expected_leaves}")
    except Exception as exc:
        raise AssertionError(f"{label}: {exc}\n{server.tail()}") from exc
    finally:
        server.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    binary = find_binary(args.build_dir)
    check_case(binary, 0)
    check_case(binary, 1)
    check_case(binary, 2)
    check_case(binary, 2, normal_close=True)
    check_case(binary, 0, unannounced_close=True)


if __name__ == "__main__":
    main()
