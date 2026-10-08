#!/usr/bin/env python3
"""Prove bid retries across PostgreSQL reply loss with a transparent wire proxy.

The proxy recognizes the prepared `place_bid` Bind frame. It cuts before Bind
is forwarded, or after PostgreSQL's ReadyForQuery (the implicit transaction has
finished) while withholding the reply. All other protocol frames pass through.
"""

import argparse
import http.client
import json
import socket
import threading
import uuid

from check_bid_atomicity import App, binary_from_roster, create_lot, pg_environment, post_bid, sql, state


def read_exact(sock, count):
    data = bytearray()
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise EOFError
        data.extend(chunk)
    return bytes(data)


def read_frame(sock):
    kind = read_exact(sock, 1)
    length = read_exact(sock, 4)
    body = read_exact(sock, int.from_bytes(length, "big") - 4)
    return kind, kind + length + body, body


class BidFaultProxy:
    def __init__(self, host, port):
        self.upstream = (host, port)
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.listener.settimeout(0.2)
        self.port = self.listener.getsockname()[1]
        self.lock = threading.Lock()
        self.target = None
        self.phase = None
        self.block_probes = False
        self.blocked = False
        self.done = threading.Event()
        self.stopped = False
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def arm(self, request_id, phase, block_probes=False):
        with self.lock:
            self.target = request_id.encode()
            self.phase = phase
            self.block_probes = block_probes
            self.blocked = False
            self.done.clear()

    def unblock(self):
        with self.lock:
            self.blocked = False

    def accept(self):
        while not self.stopped:
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with self.lock:
                blocked = self.blocked
            if blocked:
                client.close()
                continue
            threading.Thread(target=self.connection, args=(client,), daemon=True).start()

    def connection(self, client):
        try:
            server = socket.create_connection(self.upstream, timeout=8)
        except OSError:
            client.close()
            return
        client.settimeout(20)
        server.settimeout(20)
        dropping = threading.Event()
        closing = threading.Event()

        def client_to_server():
            try:
                # StartupMessage has a length but no message-type byte.
                length = read_exact(client, 4)
                server.sendall(length + read_exact(client, int.from_bytes(length, "big") - 4))
                while not closing.is_set():
                    kind, frame, body = read_frame(client)
                    phase = None
                    if kind == b"B":
                        with self.lock:
                            if self.target and b"place_bid\x00" in body:
                                phase = self.phase
                                self.target = None  # the reconciliation query must pass
                    if phase == "before":
                        self.done.set()
                        break
                    if phase == "after":
                        dropping.set()
                    server.sendall(frame)
            except (EOFError, OSError):
                pass
            finally:
                closing.set()
                client.close()
                server.close()

        def server_to_client():
            try:
                while not closing.is_set():
                    kind, frame, body = read_frame(server)
                    if dropping.is_set():
                        if kind == b"Z":
                            if body != b"I":
                                raise AssertionError(f"PostgreSQL transaction did not finish idle: {body!r}")
                            with self.lock:
                                self.blocked = self.block_probes
                            self.done.set()
                            break
                    else:
                        client.sendall(frame)
            except (EOFError, OSError):
                pass
            finally:
                closing.set()
                client.close()
                server.close()

        a = threading.Thread(target=client_to_server, daemon=True)
        b = threading.Thread(target=server_to_client, daemon=True)
        a.start()
        b.start()
        a.join(timeout=25)
        b.join(timeout=25)

    def close(self):
        self.stopped = True
        self.listener.close()
        self.thread.join(timeout=1)


def attempt(binary, app_env, db_env, proxy, lot_id, request_id, phase, block_probes=False):
    app = App(binary, app_env)
    try:
        app.wait_ready()
        conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
        try:
            conn.request("GET", f"/api/lots/{lot_id}")  # prime the old-price cache
            cached = conn.getresponse()
            cached.read()
            if cached.status != 200:
                raise AssertionError(f"could not prime lot cache: HTTP {cached.status}")
            proxy.arm(request_id, phase, block_probes)
            first = post_bid(conn, lot_id, 2, 110, request_id)
            if not proxy.done.wait(8):
                raise AssertionError(f"proxy did not cut the {phase}-commit bid connection")
            before_retry = state(db_env, lot_id)
            if phase == "after" and not block_probes and first[0] == 201:
                conn.request("GET", f"/api/lots/{lot_id}")
                refreshed = conn.getresponse()
                lot = json.loads(refreshed.read())
                if refreshed.status != 200 or lot.get("current_price") != 110:
                    raise AssertionError(f"recovered bid left stale lot cache: {refreshed.status}, {lot}")
            if phase == "after":
                sql(f"UPDATE lots SET end_time=NOW() - INTERVAL '1 second' WHERE id={lot_id}", db_env)
            proxy.unblock()
            same_worker_retry = post_bid(conn, lot_id, 2, 110, request_id)
            if same_worker_retry[0] == 201:
                conn.request("GET", f"/api/lots/{lot_id}")
                refreshed = conn.getresponse()
                lot = json.loads(refreshed.read())
                if refreshed.status != 200 or lot.get("current_price") != 110:
                    raise AssertionError(f"retry left stale lot cache: {refreshed.status}, {lot}")
        finally:
            conn.close()
        app.stop()
        return first, before_retry, same_worker_retry
    except Exception as exc:
        raise AssertionError(f"{exc}\n{app.tail()}") from exc
    finally:
        app.close()
        proxy.unblock()


def retry(binary, env, lot_id, request_id):
    app = App(binary, env)
    try:
        app.wait_ready()
        conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
        try:
            result = post_bid(conn, lot_id, 2, 110, request_id)
        finally:
            conn.close()
        app.stop()
        return result
    finally:
        app.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--phase", choices=("all", "before", "after", "after-blocked"), default="all")
    args = parser.parse_args()
    env = pg_environment()
    binary = binary_from_roster(args.build_dir)
    proxy = BidFaultProxy(env["PGHOST"], int(env["PGPORT"]))
    app_env = env.copy()
    app_env["PG_HOST"] = "127.0.0.1"
    app_env["PG_PORT"] = str(proxy.port)
    lot_ids = []
    try:
        cases = (("before", False, 503), ("after", False, 201), ("after", True, 503))
        for phase, blocked, expected_first in cases:
            if args.phase != "all" and args.phase != ("after-blocked" if blocked else phase):
                continue
            lot_id = create_lot(env, f"{phase}-{blocked}")
            lot_ids.append(lot_id)
            request_id = str(uuid.uuid4())
            (first_status, first_body), before_retry, (same_status, same_body) = attempt(
                binary, app_env, env, proxy, lot_id, request_id, phase, blocked)
            if first_status != expected_first:
                raise AssertionError(f"{phase}/{blocked}: first response {first_status}: {first_body}; "
                                     f"committed state before retry: {before_retry}")
            expected_rows = 0 if phase == "before" else 1
            if before_retry != (("100.00", 0) if expected_rows == 0 else ("110.00", 1)):
                raise AssertionError(f"{phase}/{blocked}: wrong state before retry: {before_retry}")
            if same_status != 201 or state(env, lot_id) != ("110.00", 1):
                raise AssertionError(f"same-worker retry failed after reconnect: {same_status}, {same_body}")
            retry_status, retry_body = retry(binary, app_env, lot_id, request_id)
            if retry_status != 201 or state(env, lot_id) != ("110.00", 1):
                raise AssertionError(f"{phase}/{blocked}: retry changed result: {retry_status}, {retry_body}")
            if same_body != retry_body:
                raise AssertionError(f"same ID changed response across retries: {same_body} != {retry_body}")
            if phase == "after" and not blocked and first_body != retry_body:
                raise AssertionError(f"committed reply was not stable: {first_body} != {retry_body}")
            if phase == "after" and blocked and retry_body.get("request_id") != request_id:
                raise AssertionError(f"unknown outcome did not reconcile: {retry_body}")
            print(f"PASS {phase}-commit reply loss, probe {'blocked' if blocked else 'available'}: "
                  f"{first_status}/201/201, one bid row")
    finally:
        proxy.close()
        if lot_ids:
            sql("DELETE FROM lots WHERE id IN (" + ",".join(map(str, lot_ids)) + ")", env)


if __name__ == "__main__":
    main()
