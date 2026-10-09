#!/usr/bin/env python3
"""Exercise the example's exact Lua limiter against a disposable Redis server."""

import concurrent.futures
import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Redis:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.reader = self.sock.makefile("rb")

    def close(self):
        self.reader.close()
        self.sock.close()

    def command(self, *args):
        data = [f"*{len(args)}\r\n".encode()]
        for arg in args:
            value = str(arg).encode()
            data.extend((f"${len(value)}\r\n".encode(), value, b"\r\n"))
        self.sock.sendall(b"".join(data))
        prefix = self.reader.read(1)
        line = self.reader.readline().rstrip(b"\r\n")
        if prefix == b"$":
            if line == b"-1":
                return prefix, None
            size = int(line)
            value = self.reader.read(size)
            assert self.reader.read(2) == b"\r\n"
            return prefix, value
        if prefix in (b":", b"-", b"+"):
            return prefix, int(line) if prefix == b":" else line
        if prefix == b"*":
            raise AssertionError("array reply is not expected in this test")
        raise AssertionError(f"unexpected RESP prefix {prefix!r}")


def expect(command, prefix, value):
    answer = command()
    assert answer == (prefix, value), f"expected {(prefix, value)!r}, got {answer!r}"


def script_from_example():
    source = Path(__file__).with_name("08-sorted-sets-and-ttl.cpp").read_text()
    matches = re.findall(r'SLIDING_WINDOW_SCRIPT\s*=\s*R"lua\((.*?)\)lua";', source, re.S)
    assert len(matches) == 1, "could not find the exact limiter script in the example"
    return matches[0]


def calls(client, name):
    prefix, info = client.command("INFO", "commandstats")
    assert prefix == b"$", (prefix, info)
    match = re.search(rb"^cmdstat_" + name.encode() + rb":calls=(\d+)", info, re.M)
    return int(match.group(1)) if match else 0


def read_resp(reader):
    prefix = reader.read(1)
    if not prefix:
        return None, None
    line = reader.readline()
    assert line.endswith(b"\r\n"), "incomplete RESP line"
    raw = prefix + line
    if prefix == b"*":
        values = []
        for _ in range(int(line[:-2])):
            child, value = read_resp(reader)
            raw += child
            values.append(value)
        return raw, values
    if prefix == b"$":
        size = int(line[:-2])
        if size == -1:
            return raw, None
        body = reader.read(size + 2)
        assert len(body) == size + 2 and body.endswith(b"\r\n"), "incomplete bulk reply"
        return raw + body, body[:-2]
    return raw, line[:-2]


def test_client_error_path(binary, redis_port, case):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    proxy_port = listener.getsockname()[1]
    seen = {"eval": 0, "window_ttl": 0}
    errors = []

    def serve():
        try:
            with listener.accept()[0] as client, socket.create_connection(("127.0.0.1", redis_port), timeout=20) as backend:
                client.settimeout(20)
                backend.settimeout(20)
                with client.makefile("rb") as client_in, client.makefile("wb") as client_out, \
                     backend.makefile("rb") as backend_in, backend.makefile("wb") as backend_out:
                    while True:
                        request, values = read_resp(client_in)
                        if request is None:
                            break
                        if values[0].upper() == b"EVAL":
                            seen["eval"] += 1
                            if case == "eval" and seen["eval"] == 1:
                                client_out.write(b"-ERR injected EVAL failure\r\n")
                                client_out.flush()
                                continue
                        if values[0].upper() == b"TTL" and values[1] == b"qb:example:zt:ratelimit:user42":
                            seen["window_ttl"] += 1
                            if case == "ttl":
                                client_out.write(b"-ERR injected TTL failure\r\n")
                                client_out.flush()
                                continue
                        backend_out.write(request)
                        backend_out.flush()
                        reply, _ = read_resp(backend_in)
                        client_out.write(reply)
                        client_out.flush()
        except Exception as error:
            errors.append(str(error))
        finally:
            listener.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    env = os.environ.copy()
    env["QB_EXAMPLE_REDIS_URI"] = f"tcp://127.0.0.1:{proxy_port}"
    try:
        result = subprocess.run([binary], env=env, capture_output=True, text=True, timeout=20)
    finally:
        listener.close()
        thread.join(timeout=2)
    assert not errors, errors
    assert seen == {"eval": 6, "window_ttl": 1}, seen
    assert result.returncode == 1 and "[limit] UNEXPECTED:" in result.stdout, (case, result.returncode, result.stdout, result.stderr)
    if case == "eval":
        assert "errors 1" in result.stdout, result.stdout
        print("C++ helper distinguishes one failed EVAL from one quota refusal: passed")
    else:
        assert "window key expires in n/a" in result.stdout, result.stdout
        print("C++ helper reports failed TTL as n/a and exits failed: passed")


def test_script(port, script):
    admin = Redis(port)
    key = "qb:example:window:{test}:entries"

    def decide(client, limit=1, window=1000, now=10000):
        return client.command("EVAL", script, 1, key, limit, window, now)

    try:
        # The old ZCARD/ZADD await gap admitted both calls, then reused one member.
        expect(lambda: admin.command("DEL", key), b":", 0)
        barrier = threading.Barrier(2)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            def one():
                client = Redis(port)
                try:
                    barrier.wait(timeout=5)
                    return decide(client)
                finally:
                    client.close()
            results = list(pool.map(lambda _: one(), range(2)))
        assert sorted(results) == [(b":", 0), (b":", 1)], results
        expect(lambda: admin.command("ZCARD", key), b":", 1)

        # Two accepted calls with precisely the same millisecond still occupy two members.
        expect(lambda: admin.command("DEL", key), b":", 1)
        expect(lambda: decide(admin, limit=2), b":", 1)
        expect(lambda: decide(admin, limit=2), b":", 1)
        expect(lambda: admin.command("ZCARD", key), b":", 2)
        ttl = admin.command("PTTL", key)
        assert ttl[0] == b":" and 0 < ttl[1] <= 2000, ttl

        # A pre-existing identical member must never turn an unrecorded add into admission.
        expect(lambda: admin.command("DEL", key), b":", 1)
        expect(lambda: admin.command("ZADD", key, 10000, "10000:1"), b":", 1)
        collision = decide(admin, limit=2)
        assert collision[0] == b"-" and b"collision" in collision[1], collision
        expect(lambda: admin.command("ZCARD", key), b":", 1)

        # An entry at the closed lower bound is expired, so the next call is admitted.
        expect(lambda: admin.command("DEL", key), b":", 1)
        expect(lambda: decide(admin, now=10000), b":", 1)
        expect(lambda: decide(admin, now=11000), b":", 1)
        expect(lambda: admin.command("ZCARD", key), b":", 1)

        # ACL denial occurs inside EVAL, at the exact ZADD / PEXPIRE operation.
        for denied in ("zadd", "pexpire"):
            admin.command("DEL", key)
            user = f"qb_window_no_{denied}"
            expect(lambda: admin.command("ACL", "SETUSER", user, "reset", "on", ">testpass",
                                         "~qb:example:window:*", "+eval", "+time",
                                         "+zremrangebyscore", "+zcard", "+zadd", "+pexpire", "+zrem",
                                         f"-{denied}"), b"+", b"OK")
            before_add, before_rollback = calls(admin, "zadd"), calls(admin, "zrem")
            restricted = Redis(port)
            try:
                expect(lambda: restricted.command("AUTH", user, "testpass"), b"+", b"OK")
                result = decide(restricted)
                assert result[0] == b"-" and denied.encode() in result[1].lower(), (denied, result)
            finally:
                restricted.close()
                expect(lambda: admin.command("ACL", "DELUSER", user), b":", 1)
            expect(lambda: admin.command("ZCARD", key), b":", 0)
            expect(lambda: admin.command("EXISTS", key), b":", 0)
            if denied == "pexpire":
                assert calls(admin, "zadd") == before_add + 1, "expiry failure did not follow an add"
                assert calls(admin, "zrem") == before_rollback + 1, "expiry failure did not roll back the add"
        print("atomic limit, same-ms members, NX collision, expired window, ZADD/PEXPIRE errors: passed")
    finally:
        admin.command("DEL", key)
        admin.close()


def main(binary, redis_server):
    if redis_server == "--redis-unavailable":
        print("SKIP: redis-server was unavailable at configure time")
        return 77
    with tempfile.TemporaryDirectory(prefix="qb-redis-window-") as directory:
        port = free_port()
        server = subprocess.Popen(
            [redis_server, "--bind", "127.0.0.1", "--port", str(port), "--save", "",
             "--appendonly", "no", "--dir", directory],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        try:
            for _ in range(100):
                try:
                    probe = Redis(port)
                    probe.close()
                    break
                except OSError:
                    time.sleep(0.05)
            else:
                raise RuntimeError("disposable Redis did not start")
            test_script(port, script_from_example())
            env = os.environ.copy()
            env["QB_EXAMPLE_REDIS_URI"] = f"tcp://127.0.0.1:{port}"
            result = subprocess.run([binary], env=env, capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, f"example exited {result.returncode}:\n{result.stdout}\n{result.stderr}"
            assert "[limit] one EVAL atomically" in result.stdout, result.stdout
            print("complete example on disposable Redis: passed")
            test_client_error_path(binary, port, "eval")
            test_client_error_path(binary, port, "ttl")
            verify = Redis(port)
            try:
                expect(lambda: verify.command("DBSIZE"), b":", 0)
            finally:
                verify.close()
        finally:
            server.terminate()
            try:
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
