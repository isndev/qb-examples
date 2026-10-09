#!/usr/bin/env python3
"""The feed must not keep market-data-hub alive after its publisher fails to bind."""

import argparse
import socket
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="built market-data-hub example")
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as blocker:
        if sys.platform == "win32":
            blocker.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        else:
            blocker.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        blocker.bind(("127.0.0.1", 18432))
        blocker.listen(1)
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
