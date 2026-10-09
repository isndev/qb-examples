#!/usr/bin/env python3
"""Run the cardinality example against disposable Redis and altered BITFIELD replies."""

import os
import socket
import subprocess
import sys
import tempfile
import threading
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def read_resp(reader):
    prefix = reader.read(1)
    if not prefix:
        return None, None
    line = reader.readline()
    if not line.endswith(b"\r\n"):
        raise RuntimeError("incomplete RESP line")
    raw = prefix + line
    if prefix == b"*":
        values = []
        for _ in range(int(line[:-2])):
            child, value = read_resp(reader)
            raw += child
            values.append(value)
        return raw, values
    if prefix == b"$":
        length = int(line[:-2])
        if length == -1:
            return raw, None
        body = reader.read(length + 2)
        if len(body) != length + 2 or not body.endswith(b"\r\n"):
            raise RuntimeError("incomplete RESP bulk string")
        return raw + body, body[:-2]
    return raw, line[:-2]


def remaining_keys(redis_port):
    with socket.create_connection(("127.0.0.1", redis_port), timeout=5) as sock:
        sock.sendall(b"*2\r\n$4\r\nKEYS\r\n$17\r\nqb:example:card:*\r\n")
        with sock.makefile("rb") as reader:
            _, keys = read_resp(reader)
        return keys


def run_case(binary, redis_port, case):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    proxy_port = listener.getsockname()[1]
    seen = {"commands": 0, "bitfields": 0, "injected": 0}
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
                        seen["commands"] += 1
                        if values[0].upper() == b"BITFIELD":
                            seen["bitfields"] += 1
                            position = seen["bitfields"]
                            inject = case.endswith("first") and position == 1 or case.endswith("second") and position == 2
                            if inject:
                                seen["injected"] += 1
                                response = b"-ERR injected BITFIELD refusal\r\n" if case.startswith("error") else b"*0\r\n"
                                client_out.write(response)
                                client_out.flush()
                                continue
                        backend_out.write(request)
                        backend_out.flush()
                        response, _ = read_resp(backend_in)
                        client_out.write(response)
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

    expected_code = 0 if case == "normal" else 1
    assert result.returncode == expected_code, (
        f"{case}: exit {result.returncode}, expected {expected_code}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert seen["bitfields"] == 2 and seen["injected"] == (case != "normal"), f"{case}: proxy {seen}, errors {errors}"
    assert not errors, f"{case}: proxy errors {errors}"
    if case == "normal":
        assert "#0 read back as 10, then +250 SATURATED at 255" in result.stdout, result.stdout
        assert "=== cardinality and bitmaps complete:" in result.stdout, result.stdout
    else:
        missing = "#0 read back as n/a" if case.endswith("first") else "SATURATED at n/a"
        assert missing in result.stdout, f"{case}: {result.stdout}"
        assert "[bitfield] UNEXPECTED:" in result.stderr, f"{case}: {result.stderr}"
        assert "=== cardinality and bitmaps failed" in result.stderr, f"{case}: {result.stderr}"
        assert "=== cardinality and bitmaps complete:" not in result.stdout, f"{case}: {result.stdout}"
    keys = remaining_keys(redis_port)
    assert keys == [], f"{case}: keys left after cleanup: {keys}"
    print(f"{case}: exit {result.returncode}, {seen['commands']} commands, cleanup empty")


def main(binary, redis_server):
    if redis_server == "--redis-unavailable":
        print("SKIP: redis-server was unavailable at configure time")
        return 77
    with tempfile.TemporaryDirectory(prefix="qb-redis-bitfield-") as directory:
        redis_port = free_port()
        server = subprocess.Popen(
            [redis_server, "--bind", "127.0.0.1", "--port", str(redis_port), "--save", "",
             "--appendonly", "no", "--dir", directory],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        try:
            for _ in range(100):
                try:
                    with socket.create_connection(("127.0.0.1", redis_port), timeout=0.2) as sock:
                        sock.sendall(b"*1\r\n$4\r\nPING\r\n")
                        with sock.makefile("rb") as reader:
                            _, answer = read_resp(reader)
                        if answer == b"PONG":
                            break
                except OSError:
                    time.sleep(0.05)
            else:
                raise RuntimeError("disposable Redis did not start")
            for case in ("normal", "error-first", "error-second", "short-first", "short-second"):
                run_case(binary, redis_port, case)
        finally:
            server.terminate()
            try:
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
