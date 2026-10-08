#!/usr/bin/env python3
"""Run overlapping bids through the auction example and inspect PostgreSQL state.

Four persistent HTTP sessions are opened in round-robin order. Sessions 0 and 3
must land on the same worker. A separate PostgreSQL session locks the test lot
until both bid requests have entered the server and one UPDATE is waiting on
that lock; this exercises the worker's single database connection. A second
pair covers separate workers. Additional cases pin bid eligibility, rounding,
and rollback against stored rows.
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
import uuid
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
    for name in ("PGHOSTADDR", "PGSERVICE", "PGSERVICEFILE"):
        env.pop(name, None)
    env["PGHOST"] = env.get("PG_HOST", "127.0.0.1")
    env["PGPORT"] = env.get("PG_PORT", "5432")
    env["PGUSER"] = env.get("PG_USER", "auction_user")
    env["PGDATABASE"] = env.get("PG_DB", "auction_house")
    env["PGPASSWORD"] = env.get("PG_PASS", "auction_pass")
    return env


def sql(statement, env):
    result = subprocess.run(["psql", "-X", "-q", "-t", "-A", "-h", env["PGHOST"], "-p", env["PGPORT"],
                             "-U", env["PGUSER"], "-d", env["PGDATABASE"],
                             "-v", "ON_ERROR_STOP=1", "-c", statement],
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

    def wait_bid_requests(self, lot_id, count, timeout=8):
        marker = f"[HTTP] Request: POST /api/lots/{lot_id}/bids"
        deadline = time.monotonic() + timeout
        with self.cv:
            while sum(marker in line for line in self.lines) < count:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or self.proc.poll() is not None:
                    raise AssertionError(f"expected {count} bid requests to reach HTTP middleware for lot {lot_id}")
                self.cv.wait(min(remaining, 0.2))

    def wait_ready(self):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and self.proc.poll() is None:
            try:
                conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=2)
                with self.cv:
                    count = len(self.workers) + 1
                conn.request("GET", "/health")
                response = conn.getresponse()
                body = json.loads(response.read())
                conn.close()
                self.wait_workers(count)  # consume this readiness connection's log before opening another
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
        worker = self.wait_workers(next_count)
        with self.cv:
            if len(self.workers) != next_count:
                raise AssertionError("another HTTP session arrived while identifying this worker")
        return conn, worker

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


def create_lot(env, suffix, status="active", future_start=False):
    title = f"qb-bid-check-{os.getpid()}-{time.time_ns()}-{suffix}"
    if status not in ("active", "cancelled"):
        raise AssertionError(f"unsupported test status: {status}")
    start_time = "NOW() + INTERVAL '30 minutes'" if future_start else "NOW() - INTERVAL '1 minute'"
    statement = ("INSERT INTO lots (title, description, category, start_price, current_price, seller_id, status, start_time, end_time) "
                 f"VALUES ('{title}', 'bid atomicity check', 'general', 100, 100, "
                 f"(SELECT id FROM users WHERE username = 'alice'), '{status}', {start_time}, "
                 "NOW() + INTERVAL '1 hour') RETURNING id")
    return int(sql(statement, env))


def state(env, lot_id):
    result = sql("SELECT l.current_price::text, COUNT(b.id) FROM lots l "
                 f"LEFT JOIN bids b ON b.lot_id=l.id WHERE l.id={lot_id} "
                 "GROUP BY l.id,l.current_price", env)
    price, count = result.split("|")
    return price, int(count)


def post_bid(conn, lot_id, bidder_id, amount, request_id=None):
    body = json.dumps({"bidder_id": bidder_id, "amount": amount,
                       "request_id": request_id or str(uuid.uuid4())}).encode()
    conn.request("POST", f"/api/lots/{lot_id}/bids", body=body,
                 headers={"Content-Type": "application/json", "Connection": "keep-alive"})
    response = conn.getresponse()
    payload = json.loads(response.read())
    return response.status, payload


class LotLock:
    """Hold a test lot's row lock in an independent PostgreSQL transaction."""

    def __init__(self, env, lot_id):
        self.proc = subprocess.Popen(
            ["psql", "-X", "-q", "-t", "-A", "-h", env["PGHOST"], "-p", env["PGPORT"],
             "-U", env["PGUSER"], "-d", env["PGDATABASE"], "-v", "ON_ERROR_STOP=1"],
            env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.lines = []
        self.cv = threading.Condition()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        try:
            self.proc.stdin.write(f"BEGIN;\nSELECT id FROM lots WHERE id={lot_id} FOR UPDATE;\n\\echo LOT_LOCKED\n")
            self.proc.stdin.flush()
            self.wait_line("LOT_LOCKED")
        except Exception:
            if self.proc.poll() is None:
                self.proc.kill()
            self.proc.wait(timeout=3)
            self.reader.join(timeout=1)
            raise

    def _read(self):
        for line in self.proc.stdout:
            with self.cv:
                self.lines.append(line.rstrip())
                self.cv.notify_all()

    def wait_line(self, marker, timeout=8):
        deadline = time.monotonic() + timeout
        with self.cv:
            while not any(marker in line for line in self.lines):
                remaining = deadline - time.monotonic()
                if remaining <= 0 or self.proc.poll() is not None:
                    raise AssertionError(f"could not lock test lot: {self.lines[-10:]}")
                self.cv.wait(min(remaining, 0.2))

    def release(self):
        if self.proc.poll() is None:
            self.proc.stdin.write("COMMIT;\n\\q\n")
            self.proc.stdin.flush()
            self.proc.stdin.close()
        try:
            code = self.proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=3)
            raise AssertionError("lot-lock transaction did not exit")
        self.reader.join(timeout=1)
        if code != 0:
            raise AssertionError(f"lot-lock transaction failed: {self.lines[-10:]}")


def wait_for_lock_waiter(env, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        waiting = sql("SELECT COUNT(*) FROM pg_stat_activity "
                      "WHERE datname=current_database() AND wait_event_type='Lock'", env)
        if int(waiting) > 0:
            return
        time.sleep(0.05)
    raise AssertionError("no auction worker reached the locked UPDATE")


def concurrent_bids(first, second, lot_id, app, env):
    barrier = threading.Barrier(3)

    def send(conn, bidder):
        barrier.wait()
        return post_bid(conn, lot_id, bidder, 110)

    lock = LotLock(env, lot_id)
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            a = pool.submit(send, first, 2)
            b = pool.submit(send, second, 3)
            barrier.wait()
            try:
                wait_for_lock_waiter(env)
                app.wait_bid_requests(lot_id, 2)
            finally:
                lock.release()
            result = [a.result(timeout=10), b.result(timeout=10)]
    finally:
        if lock.proc.poll() is None:
            lock.release()
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
            concurrent_bids(sessions[0][0], sessions[3][0], same_lot, app, env)
        finally:
            for conn, _ in sessions:
                conn.close()
        if state(env, same_lot) != ("110.00", 1):
            raise AssertionError(f"same-worker bids left {state(env, same_lot)}")
        print("PASS same worker: 201/409, one bid row, price 110.00")

        # An INSERT failure must roll back the preceding UPDATE in that statement.
        bad, _ = app.session()
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
            concurrent_bids(cross[0][0], cross[1][0], other_lot, app, env)
        finally:
            for conn, _ in cross:
                conn.close()
        if state(env, other_lot) != ("110.00", 1):
            raise AssertionError(f"cross-worker bids left {state(env, other_lot)}")
        print("PASS distinct workers: 201/409, one bid row, price 110.00")

        for label, options in (("cancelled", {"status": "cancelled"}), ("not started", {"future_start": True})):
            blocked_lot = create_lot(env, label.replace(" ", "-"), **options)
            lot_ids.append(blocked_lot)
            conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
            try:
                status, _ = post_bid(conn, blocked_lot, 2, 110)
            finally:
                conn.close()
            if status != 409 or state(env, blocked_lot) != ("100.00", 0):
                raise AssertionError(f"{label} lot accepted a bid: status={status}, state={state(env, blocked_lot)}")
            print(f"PASS {label} lot: 409, price 100.00, zero bid rows")

        rounded_lot = create_lot(env, "fractional-cents")
        lot_ids.append(rounded_lot)
        conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
        try:
            status, payload = post_bid(conn, rounded_lot, 2, 110.009)
        finally:
            conn.close()
        if status != 201 or state(env, rounded_lot) != ("110.01", 1) or abs(payload.get("new_price", 0) - 110.01) > 1e-9:
            raise AssertionError(f"rounded bid reply disagrees with row: status={status}, body={payload}, state={state(env, rounded_lot)}")
        print("PASS fractional cents: response and stored price both 110.01")

        replay_lot = create_lot(env, "replay")
        lot_ids.append(replay_lot)
        request_id = str(uuid.uuid4())
        conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
        try:
            missing = json.dumps({"bidder_id": 2, "amount": 110}).encode()
            conn.request("POST", f"/api/lots/{replay_lot}/bids", body=missing,
                         headers={"Content-Type": "application/json"})
            response = conn.getresponse()
            response.read()
            if response.status != 400 or state(env, replay_lot) != ("100.00", 0):
                raise AssertionError("bid without request_id was not rejected before DB mutation")
            first_status, first_body = post_bid(conn, replay_lot, 2, 110, request_id)
            if first_status != 201:
                raise AssertionError(f"first keyed bid failed: {first_status}, {first_body}")
            status, _ = post_bid(conn, replay_lot, 3, 120)
            if status != 201:
                raise AssertionError("follow-on bid failed")
            sql(f"UPDATE lots SET end_time=NOW() - INTERVAL '1 second' WHERE id={replay_lot}", env)
            repeated_status, repeated_body = post_bid(conn, replay_lot, 2, 110, request_id)
            if repeated_status != 201 or repeated_body != first_body or state(env, replay_lot) != ("120.00", 2):
                raise AssertionError(f"repeat did not preserve first response: {repeated_status}, {repeated_body}")
            conflict_status, _ = post_bid(conn, replay_lot, 2, 130, request_id)
            if conflict_status != 409 or state(env, replay_lot) != ("120.00", 2):
                raise AssertionError("reused request_id changed price or bid history")
            print("PASS request identity: missing 400, exact replay 201, changed payload 409")
        finally:
            conn.close()
        app.stop()
    except Exception as exc:
        raise AssertionError(f"{exc}\n{app.tail()}") from exc
    finally:
        app.close()
        if lot_ids:
            sql("DELETE FROM lots WHERE id IN (" + ",".join(map(str, lot_ids)) + ")", env)


if __name__ == "__main__":
    main()
