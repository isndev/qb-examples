#!/usr/bin/env python3
"""Exercise chat shutdown with live WebSocket sessions on a loopback port.

The ordinary example runner keeps the server alive but does not stop it while
an announced client is connected. That order used to recurse from a session
destructor and crash. Also exercise the manual client's Close reply and both
reconnect paths with a loopback peer. Run with a built superproject roster.
"""

import argparse
import base64
import hashlib
import json
import os
import signal
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path


TARGET = "qb-example-modules-ws-chat-server"
CLIENT_TARGET = "qb-example-modules-ws-chat-client"


def find_binary(build_dir, target=TARGET):
    roster = Path(build_dir) / "examples" / "example-roster.txt"
    matches = [line.split("|") for line in roster.read_text().splitlines()
               if line.startswith(f"built|{target}|")]
    if len(matches) != 1 or len(matches[0]) != 4:
        raise AssertionError(f"expected exactly one built {target} in {roster}")
    binary = Path(matches[0][3])
    if not binary.is_file():
        raise AssertionError(f"missing built example: {binary}")
    return binary


def recv_exact(peer, count):
    data = bytearray()
    while len(data) < count:
        chunk = peer.recv(min(count - len(data), 65536))
        if not chunk:
            raise AssertionError(f"socket closed after {len(data)} of {count} bytes")
        data.extend(chunk)
    return bytes(data)


def recv_frame_header(peer):
    first, second = recv_exact(peer, 2)
    size = second & 0x7f
    if size == 126:
        size = int.from_bytes(recv_exact(peer, 2), "big")
    elif size == 127:
        size = int.from_bytes(recv_exact(peer, 8), "big")
    mask = recv_exact(peer, 4) if second & 0x80 else b""
    return first & 0x0f, bool(mask), size, mask


def recv_frame(peer):
    opcode, masked, size, mask = recv_frame_header(peer)
    payload = recv_exact(peer, size)
    if mask:
        payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return opcode, masked, payload


def expect_tcp_closed(peer):
    try:
        data = peer.recv(1)
    except (ConnectionResetError, ConnectionAbortedError):
        return
    if data:
        raise AssertionError(f"first TCP connection remained open: {data!r}")


class ClientPeer:
    def __init__(self, binary):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(2)
        self.listener.settimeout(4)
        self.url = f"ws://127.0.0.1:{self.listener.getsockname()[1]}/ws"
        self.tempdir = tempfile.TemporaryDirectory(prefix="qb-ws-client-")
        self.proc = subprocess.Popen([str(binary)], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     cwd=self.tempdir.name)
        self.peers = []
        self.keys = []
        self.output = bytearray()
        self.cv = threading.Condition()
        self.reader = threading.Thread(target=self._read_output, daemon=True)
        self.reader.start()

    def _read_output(self):
        while chunk := os.read(self.proc.stdout.fileno(), 65536):
            with self.cv:
                self.output.extend(chunk)
                self.cv.notify_all()

    def wait_output(self, text, timeout=8):
        needle = text.encode()
        deadline = time.monotonic() + timeout
        with self.cv:
            while needle not in self.output:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or self.proc.poll() is not None:
                    tail = bytes(self.output[-300:]).decode(errors="replace")
                    raise AssertionError(f"client did not print {text!r}; exit={self.proc.poll()}, bytes={len(self.output)}, tail={tail!r}")
                self.cv.wait(min(remaining, 0.1))

    def has_output(self, text):
        with self.cv:
            return text.encode() in self.output

    def command(self, command):
        self.proc.stdin.write((command + "\n").encode())
        self.proc.stdin.flush()

    def accept_upgrade(self):
        peer, _ = self.listener.accept()
        peer.settimeout(3)
        self.peers.append(peer)
        request = b""
        while b"\r\n\r\n" not in request and len(request) < 8192:
            request += peer.recv(4096)
        if b"\r\n\r\n" not in request:
            raise AssertionError(f"missing WebSocket handshake: {request[:160]!r}")
        headers = request.decode("ascii").split("\r\n")
        if headers[0] != "GET /ws HTTP/1.1":
            raise AssertionError(f"unexpected request: {headers[0]}")
        key = next((line.split(":", 1)[1].strip() for line in headers[1:]
                    if line.lower().startswith("sec-websocket-key:")), None)
        if key is None:
            raise AssertionError("missing Sec-WebSocket-Key")
        self.keys.append(key)
        accept = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        peer.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                      "Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n").encode())
        opcode, masked, payload = recv_frame(peer)
        if opcode != 1 or not masked or json.loads(payload).get("type") != "user_joined":
            raise AssertionError(f"missing masked user_joined: {opcode}, {masked}, {payload!r}")
        return peer

    def stop(self):
        if self.proc.poll() is None:
            self.command("/quit")
        self.proc.stdin.close()
        try:
            code = self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=3)
            raise AssertionError("chat client did not stop within five seconds")
        self.reader.join(timeout=1)
        if self.reader.is_alive():
            raise AssertionError("client output was not fully read after exit")
        output = self.output.decode(errors="replace")
        if code != 0:
            raise AssertionError(f"chat client exited {code}: {output[-1500:]}")
        return output

    def close(self):
        for peer in self.peers:
            peer.close()
        self.listener.close()
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=3)
        self.reader.join(timeout=1)
        self.proc.stdout.close()
        if not self.proc.stdin.closed:
            self.proc.stdin.close()
        self.tempdir.cleanup()


def check_client_case(binary, peer_close):
    client = ClientPeer(binary)
    label = "peer-close then reconnect" if peer_close else "/disconnect then /connect"
    try:
        client.command(f"/connect {client.url}")
        first = client.accept_upgrade()
        if peer_close:
            first.sendall(b"\x88\x02\x03\xe8")
            opcode, masked, payload = recv_frame(first)
            if opcode != 8 or not masked or payload != b"\x03\xe8":
                raise AssertionError(f"Close reply: opcode={opcode}, masked={masked}, payload={payload!r}")
        else:
            client.command("/disconnect")
        expect_tcp_closed(first)
        client.command(f"/connect {client.url}")
        client.accept_upgrade()
        if client.keys[0] == client.keys[1]:
            raise AssertionError("reconnect reused its WebSocket handshake key")
        output = client.stop()
        if output.count("Disconnected from server") != 2:
            raise AssertionError("expected one disconnect callback per connection")
        if peer_close and output.count("Connection closed:") != 1:
            raise AssertionError("expected one peer Close callback")
        print(f"PASS client {label}: two handshakes, bounded exit")
    except Exception as exc:
        raise AssertionError(f"client {label}: {exc}") from exc
    finally:
        client.close()


def check_client_backpressure(binary):
    client = ClientPeer(binary)
    try:
        client.command(f"/connect {client.url}")
        first = client.accept_upgrade()
        first.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        first.settimeout(8)
        # Ping replies have no CLI echo. Hold back their reads to fill the
        # client's TCP send buffer before it appends its Close reply.
        payload = b"x" * 125
        ping = b"\x89\x7d" + payload
        count = 8192
        first.sendall(ping * count + b"\x88\x02\x03\xe8")
        client.wait_output("Connection closed:")
        time.sleep(0.1)
        if client.has_output("Disconnected from server"):
            raise AssertionError("backpressure setup did not hold the old connection open")
        client.command(f"/connect {client.url}")
        client.wait_output("Close in progress")

        for index in range(count):
            opcode, masked, reply = recv_frame(first)
            if opcode != 10 or not masked or reply != payload:
                raise AssertionError(f"pong {index}: {opcode}, {masked}, {reply!r}")
        opcode, masked, payload = recv_frame(first)
        if opcode != 8 or not masked or payload != b"\x03\xe8":
            raise AssertionError(f"partial-write Close reply: {opcode}, {masked}, {payload!r}")
        expect_tcp_closed(first)
        client.command(f"/connect {client.url}")
        client.accept_upgrade()
        output = client.stop()
        if output.count("Disconnected from server") != 2:
            raise AssertionError("expected one disconnect callback per connection")
        print(f"PASS client blocked Close: {count} masked Pong, complete masked Close, EOF, reconnect")
    except Exception as exc:
        raise AssertionError(f"client blocked Close: {exc}") from exc
    finally:
        client.close()


class Server:
    def __init__(self, binary):
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            self.port = reserved.getsockname()[1]
        self.tempdir = tempfile.TemporaryDirectory(prefix="qb-ws-server-")
        self.proc = subprocess.Popen(
            [str(binary), "--port", str(self.port)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            bufsize=1, cwd=self.tempdir.name)
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
        self.tempdir.cleanup()

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
    parser.add_argument("--client-only", action="store_true")
    parser.add_argument("--client-binary", type=Path)
    parser.add_argument("--client-case", choices=("all", "peer-close", "disconnect", "backpressure"), default="all")
    args = parser.parse_args()
    if args.client_only:
        client_binary = args.client_binary or find_binary(args.build_dir, CLIENT_TARGET)
        if args.client_case in ("all", "peer-close"):
            check_client_case(client_binary, peer_close=True)
        if args.client_case in ("all", "disconnect"):
            check_client_case(client_binary, peer_close=False)
        if args.client_case in ("all", "backpressure"):
            check_client_backpressure(client_binary)
        return
    binary = find_binary(args.build_dir)
    check_case(binary, 0)
    check_case(binary, 1)
    check_case(binary, 2)
    check_case(binary, 2, normal_close=True)
    check_case(binary, 0, unannounced_close=True)
    client_binary = args.client_binary or find_binary(args.build_dir, CLIENT_TARGET)
    check_client_case(client_binary, peer_close=True)
    check_client_case(client_binary, peer_close=False)
    check_client_backpressure(client_binary)


if __name__ == "__main__":
    main()
