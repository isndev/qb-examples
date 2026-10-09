#!/usr/bin/env python3
"""Smoke-test TaskManager's HTTP-to-WebSocket handoff against a running server."""

import json
import socket
import sys
import urllib.request


host, port = "127.0.0.1", 8080
if len(sys.argv) > 1:
    host, port_text = sys.argv[1].rsplit(":", 1)
    port = int(port_text)


def exchange(headers, expect_close=False):
    request = f"GET /ws HTTP/1.1\r\nHost: {host}:{port}\r\n{headers}\r\n".encode("ascii")
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.settimeout(5)
        sock.sendall(request)
        reply = bytearray()
        while len(reply) < 8192:
            chunk = sock.recv(8192 - len(reply))
            if not chunk:
                return bytes(reply), True
            reply.extend(chunk)
            if not expect_close and b"\r\n\r\n" in reply:
                return bytes(reply), False
    raise RuntimeError("response exceeded 8192 bytes")


with urllib.request.urlopen(f"http://{host}:{port}/health", timeout=5) as response:
    if response.status != 200 or json.load(response)["status"] != "ok":
        raise RuntimeError("/health did not return status ok")
print("/health: 200")

reply, _ = exchange("")
if not reply.startswith(b"HTTP/1.1 400 "):
    raise RuntimeError(f"plain GET /ws should return 400: {reply!r}")
print("plain GET /ws: 400 before handoff")

upgrade_headers = (
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Version: 13\r\n"
)
reply, closed = exchange(upgrade_headers + "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n")
if closed or not reply.startswith(b"HTTP/1.1 101 ") or b"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" not in reply:
    raise RuntimeError(f"valid upgrade should return the RFC 6455 handshake: {reply!r}")
print("valid GET /ws: 101 with correct accept key")

reply, closed = exchange(upgrade_headers + "Sec-WebSocket-Key: shortkey\r\n", expect_close=True)
if not closed or reply:
    raise RuntimeError(f"rejected post-handoff key should close without a second HTTP response: {reply!r}")
print("invalid key after handoff: connection closed without HTTP response")
