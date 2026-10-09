#!/usr/bin/env python3
"""Market-data-hub must exit when either end of its local wire cannot run."""

import argparse
import os
import socket
import subprocess
import sys
import threading
import time


def reserved_port(port: int, listen: bool = False) -> socket.socket:
    blocker = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        if sys.platform == "win32":
            blocker.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        else:
            blocker.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        blocker.bind(("127.0.0.1", port))
        if listen:
            blocker.listen(1)
        return blocker
    except OSError:
        blocker.close()
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="built market-data-hub example")
    args = parser.parse_args()

    with reserved_port(18432, listen=True):
        try:
            result = subprocess.run(
                [args.binary], capture_output=True, text=True, timeout=5, check=False
            )
        except subprocess.TimeoutExpired:
            print("FAIL: publisher bind failed, but the feed thread kept the process alive past 5 s")
            return 1

    output = result.stdout + result.stderr
    if result.returncode == 0:
        print("FAIL: publisher bind failure returned success")
        return 1
    if "could not bind 127.0.0.1:18432" not in output or "Core Init Failed" not in output:
        print("FAIL: the blocked port did not exercise publisher startup failure")
        print(output[-1000:])
        return 1
    if "=== market-data-hub complete: FAILED" not in output:
        print("FAIL: the program did not report its failed pipeline verdict")
        print(output[-1000:])
        return 1

    print("PASS: publisher bind failure exited nonzero after joining the feed")

    # A bound socket with no listener reserves the subscriber's target port while
    # making connect() fail. The publisher still starts successfully on 18432.
    with reserved_port(18433):
        env = os.environ.copy()
        env["QB_MARKET_DATA_TEST_SUBSCRIBER_PORT"] = "1"
        try:
            result = subprocess.run(
                [args.binary], capture_output=True, text=True, timeout=5, check=False, env=env
            )
        except subprocess.TimeoutExpired:
            print("FAIL: subscriber connect failed after startup, but the engine kept running past 5 s")
            return 1

    output = result.stdout + result.stderr
    if result.returncode == 0 or "[subscriber] could not connect" not in output:
        print("FAIL: subscriber connect failure did not produce a failed verdict")
        print(output[-1000:])
        return 1
    if "=== market-data-hub complete: FAILED" not in output:
        print("FAIL: subscriber connect failure did not reach the final verdict")
        print(output[-1000:])
        return 1

    print("PASS: subscriber connect failure stopped the engine and joined the feed")

    # The TCP connect succeeds to this temporary peer, which then closes before
    # sending the wire sentinel. The subscriber's disconnected event must stop
    # the engine once; a connect failure would not exercise this callback.
    with reserved_port(18433, listen=True) as peer:
        peer.settimeout(6)
        accepted = threading.Event()
        peer_errors: list[str] = []

        def close_after_accept() -> None:
            try:
                conn, _ = peer.accept()
                accepted.set()
                time.sleep(1.5)
                conn.close()
            except OSError as error:
                peer_errors.append(str(error))

        worker = threading.Thread(target=close_after_accept, daemon=True)
        worker.start()
        try:
            result = subprocess.run(
                [args.binary], capture_output=True, text=True, timeout=7, check=False, env=env
            )
        except subprocess.TimeoutExpired:
            print("FAIL: the peer closed before the sentinel, but the engine kept running past 7 s")
            return 1
        worker.join(timeout=2)

    output = result.stdout + result.stderr
    if peer_errors or not accepted.is_set() or "[subscriber] could not connect" in output:
        print("FAIL: the early-disconnect control did not establish a subscriber connection")
        print(peer_errors, output[-1000:])
        return 1
    if result.returncode == 0 or "[subscriber] disconnected before end-of-stream" not in output:
        print("FAIL: early disconnect did not stop the engine with a failed verdict")
        print(output[-1000:])
        return 1
    if "=== market-data-hub complete: FAILED" not in output:
        print("FAIL: early disconnect did not reach the final verdict")
        return 1

    print("PASS: early subscriber disconnect stopped the engine and joined the feed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
