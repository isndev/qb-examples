#!/usr/bin/env python3
"""Run overlapping bids through the auction example and inspect PostgreSQL state.

Four persistent HTTP sessions are opened in round-robin order. Sessions 0 and 3
must land on the same worker; sending from both at once exercises the worker's
single database connection. A second pair covers separate workers.
"""

import argparse
import concurrent.futures
import http.client
import json
import os
import re
import signal
import socket
import subprocess
import threading
import time
from pathlib import Path


TARGET = "qb-example-applications-auction-house"
SESSION = re.compile(r"\[AuctionManager (\d+)\] HTTP session")


def binary_from_roster(build_dir):
    roster = Path(build_dir) / "examples" / "example-roster.txt"
    records = [line.split("|") for line in roster.read_text().splitlines()
               if line.startswith(f"built|{TARGET}|")]
    if len(records) != 1 or len(records[0]) != 4:
        raise AssertionError(f"expected one built {TARGET} in {roster}")
    binary = Path(records[0][3])
    if not binary.is_file():
        raise AssertionError(f"missing executable: {binary}")
    return binary


def pg_environment():
    env = os.environ.copy()
    env["PGHOST"] = env.get("PG_HOST", "127.0.0.1")
    env["PGUSER"] = env.get("PG_USER", "auction_user")
    env["PGDATABASE"] = env.get("PG_DB", "auction_house")
    env["PGPASSWORD"] = env.get("PG_PASS", "auction_pass")
    return env


def sql(statement, env):
    result = subprocess.run(["psql", "-X", "-q", "-t", "-A", "-v", "ON_ERROR_STOP=1", "-c", statement],
                            env=env, text=True, capture_output=True, timeout=8, check=True)
    return result.stdout.strip()


class App:
    def __init__(self, binary, env):
        with socket.socket() as probe:
            probe.settimeout(0.3)
            if probe.connect_ex(("127.0.0.1", 8080)) == 0:
                raise AssertionError("port 8080 is occupied; refusing to use another server")
        self.proc = subprocess.Popen([str(binary)], env=env, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.lines = []
        self.workers = []
        self.cv = threading.Condition()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.proc.stdout:
            with self.cv:
                if len(self.lines) < 2000:
                    self.lines.append(line.rstrip())
                if match := SESSION.search(line):
                    self.workers.append(match.group(1))
                self.cv.notify_all()

    def wait_workers(self, count):
        deadline = time.monotonic() + 5
        with self.cv:
            while len(self.workers) < count:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or self.proc.poll() is not None:
                    raise AssertionError(f"expected {count} accepted sessions, got {len(self.workers)}")
                self.cv.wait(min(remaining, 0.2))
            return self.workers[count - 1]

    def wait_ready(self):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and self.proc.poll() is None:
            try:
                conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=2)
                conn.request("GET", "/health")
                response = conn.getresponse()
                body = json.loads(response.read())
                conn.close()
                if response.status == 200 and body.get("db_ready") and body.get("redis_ready"):
                    return
            except (OSError, ValueError, http.client.HTTPException):
                pass
            time.sleep(0.1)
        raise AssertionError("auction application did not become ready")

    def session(self):
        conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
        with self.cv:
            next_count = len(self.workers) + 1
        conn.request("GET", "/health", headers={"Connection": "keep-alive"})
        response = conn.getresponse()
        response.read()
        if response.status != 200 or conn.sock is None:
            raise AssertionError("health request did not leave a persistent HTTP session")
        return conn, self.wait_workers(next_count)

    def stop(self):
        if self.proc.poll() is not None:
            raise AssertionError(f"auction application exited early: {self.proc.returncode}")
        self.proc.send_signal(signal.SIGTERM)
        try:
            code = self.proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=3)
            raise AssertionError("auction application did not stop")
        if code != 0:
            raise AssertionError(f"auction application stopped with {code}")

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=3)
        self.reader.join(timeout=1)

    def tail(self):
        with self.cv:
            return "\n".join(self.lines[-25:])


def create_lot(env, suffix):
    title = f"qb-bid-check-{os.getpid()}-{time.time_ns()}-{suffix}"
    statement = ("INSERT INTO lots (title, description, category, start_price, current_price, seller_id, end_time) "
                 f"VALUES ('{title}', 'bid atomicity check', 'general', 100, 100, "
                 "(SELECT id FROM users WHERE username = 'alice'), NOW() + INTERVAL '1 hour') RETURNING id")
    return int(sql(statement, env))


def state(env, lot_id):
    result = sql("SELECT l.current_price::text, COUNT(b.id) FROM lots l "
                 f"LEFT JOIN bids b ON b.lot_id=l.id WHERE l.id={lot_id} "
                 "GROUP BY l.id,l.current_price", env)
    price, count = result.split("|")
    return price, int(count)


def post_bid(conn, lot_id, bidder_id, amount):
    body = json.dumps({"bidder_id": bidder_id, "amount": amount}).encode()
    conn.request("POST", f"/api/lots/{lot_id}/bids", body=body,
                 headers={"Content-Type": "application/json", "Connection": "keep-alive"})
    response = conn.getresponse()
    payload = json.loads(response.read())
    return response.status, payload


def concurrent_bids(first, second, lot_id):
    barrier = threading.Barrier(3)

    def send(conn, bidder):
        barrier.wait()
        return post_bid(conn, lot_id, bidder, 110)

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        a = pool.submit(send, first, 2)
        b = pool.submit(send, second, 3)
        barrier.wait()
        result = [a.result(timeout=10), b.result(timeout=10)]
    if sorted(status for status, _ in result) != [201, 409]:
        raise AssertionError(f"expected one accepted and one rejected bid, got {result}")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    env = pg_environment()
    app = App(binary_from_roster(args.build_dir), env)
    lot_ids = []
    try:
        app.wait_ready()
        same_lot = create_lot(env, "same")
        lot_ids.append(same_lot)

        # The acceptor distributes connections round-robin over three workers.
        sessions = [app.session() for _ in range(4)]
        try:
            if sessions[0][1] != sessions[3][1]:
                raise AssertionError(f"first and fourth sessions differ: {[worker for _, worker in sessions]}")
            concurrent_bids(sessions[0][0], sessions[3][0], same_lot)
        finally:
            for conn, _ in sessions:
                conn.close()
        if state(env, same_lot) != ("110.00", 1):
            raise AssertionError(f"same-worker bids left {state(env, same_lot)}")
        print("PASS same worker: 201/409, one bid row, price 110.00")

        # An INSERT failure must roll back the preceding UPDATE in that statement.
        bad = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
        try:
            status, _ = post_bid(bad, same_lot, 999999, 120)
            if status != 409 or state(env, same_lot) != ("110.00", 1):
                raise AssertionError("failed INSERT changed price or bid history")
            status, _ = post_bid(bad, same_lot, 2, 120)
            if status != 201 or state(env, same_lot) != ("120.00", 2):
                raise AssertionError("connection did not recover after failed INSERT")
            print("PASS failed INSERT: rollback, then valid bid accepted")
        finally:
            bad.close()

        other_lot = create_lot(env, "different")
        lot_ids.append(other_lot)
        cross = [app.session() for _ in range(2)]
        try:
            if cross[0][1] == cross[1][1]:
                raise AssertionError("cross-worker sessions unexpectedly share a worker")
            concurrent_bids(cross[0][0], cross[1][0], other_lot)
        finally:
            for conn, _ in cross:
                conn.close()
        if state(env, other_lot) != ("110.00", 1):
            raise AssertionError(f"cross-worker bids left {state(env, other_lot)}")
        print("PASS distinct workers: 201/409, one bid row, price 110.00")
        app.stop()
    except Exception as exc:
        raise AssertionError(f"{exc}\n{app.tail()}") from exc
    finally:
        app.close()
        if lot_ids:
            sql("DELETE FROM lots WHERE id IN (" + ",".join(map(str, lot_ids)) + ")", env)


if __name__ == "__main__":
    main()
