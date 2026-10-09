#!/usr/bin/env python3
"""Check all five actor results and shutdown against a disposable ACL-restricted Redis."""

import os
import socket
import subprocess
import sys
import tempfile
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def command(port, *args):
    request = [f"*{len(args)}\r\n".encode()]
    for arg in args:
        value = str(arg).encode()
        request.extend((f"${len(value)}\r\n".encode(), value, b"\r\n"))
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.sendall(b"".join(request))
        with sock.makefile("rb") as reader:
            prefix = reader.read(1)
            line = reader.readline().rstrip(b"\r\n")
            if prefix == b":":
                return prefix, int(line)
            if prefix in (b"+", b"-"):
                return prefix, line
            if prefix == b"$":
                size = int(line)
                if size == -1:
                    return prefix, None
                body = reader.read(size)
                assert reader.read(2) == b"\r\n"
                return prefix, body
            raise AssertionError(f"unexpected Redis reply: {prefix!r} {line!r}")


def run_case(binary, port, allowed_data, work_dir):
    env = os.environ.copy()
    env["QB_EXAMPLE_REDIS_URI"] = f"tcp://127.0.0.1:{port}"
    try:
        result = subprocess.run([binary], cwd=work_dir, env=env, capture_output=True, text=True, timeout=7)
    except subprocess.TimeoutExpired as error:
        raise AssertionError(f"actor did not stop after seven seconds (allowed_data={allowed_data})") from error

    output = result.stdout + result.stderr
    expected_code = 0 if allowed_data == 5 else 1
    assert result.returncode == expected_code, (allowed_data, result.returncode, output)
    assert output.count("MainActor: Received work result ") == 5, output
    assert output.count(" (ok)") == allowed_data, output
    assert output.count(" (failed)") == 5 - allowed_data, output
    assert output.count("Received shutdown request") == 1, output
    assert output.count("RedisWorkerActor shutting down") == 1, output
    assert "Engine stopped, all actors terminated" in output, output
    assert output.count("SET failed for key: async:data:") == 5 - allowed_data, output
    assert output.count("Data stored successfully at key: async:data:") == allowed_data, output
    assert f"Final counter value: {allowed_data}" in output, output
    assert f"Deleted {allowed_data + 1} key(s) written by this run" in output, output
    if expected_code:
        assert "Redis Async Operations Example failed" in output, output
        assert "Redis Async Operations Example completed" not in output, output
    else:
        assert "Redis Async Operations Example completed" in output, output
    assert command(port, "EXISTS", "async:counter") == (b":", 0), output
    for index in range(1, allowed_data + 1):
        assert command(port, "EXISTS", f"async:data:{index}") == (b":", 0), output
    print(f"{allowed_data}/5 permitted: five results, one shutdown, exit {result.returncode}, keys cleaned")


def main(binary, redis_server):
    if redis_server == "--redis-unavailable":
        print("SKIP: redis-server was unavailable at configure time")
        return 77

    with tempfile.TemporaryDirectory(prefix="qb-redis-cache-actor-") as directory:
        port = free_port()
        server = subprocess.Popen(
            [redis_server, "--bind", "127.0.0.1", "--port", str(port), "--save", "",
             "--appendonly", "no", "--dir", directory],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        try:
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError(f"disposable Redis exited {server.returncode}")
                try:
                    if command(port, "PING") == (b"+", b"PONG"):
                        break
                except OSError:
                    time.sleep(0.05)
            else:
                raise RuntimeError("disposable Redis did not start")

            run_case(binary, port, allowed_data=5, work_dir=directory)
            # SET async:counter remains allowed; SET async:data:* is refused by real Redis ACL.
            assert command(port, "ACL", "SETUSER", "default", "reset", "on", "nopass",
                           "~async:counter", "~async:data:1", "~async:data:2",
                           "~async:data:3", "~async:data:4", "+@all") == (b"+", b"OK")
            run_case(binary, port, allowed_data=4, work_dir=directory)
            assert command(port, "ACL", "SETUSER", "default", "reset", "on", "nopass",
                           "~async:counter", "+@all") == (b"+", b"OK")
            assert command(port, "ACL", "DRYRUN", "default", "SET", "async:counter", "0") == (b"+", b"OK")
            denied = command(port, "ACL", "DRYRUN", "default", "SET", "async:data:1", "value")
            assert denied[0] == b"$" and b"key" in denied[1].lower(), denied
            run_case(binary, port, allowed_data=0, work_dir=directory)
        finally:
            server.terminate()
            try:
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
