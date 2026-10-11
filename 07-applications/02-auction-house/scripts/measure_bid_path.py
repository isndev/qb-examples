#!/usr/bin/env python3
"""Measure accepted bid HTTP latency on one persistent session and live PostgreSQL.

Run the same script against before/after binaries on an otherwise idle host.
The UUID is sent in both runs; the older handler ignores the extra JSON field.
"""

import argparse
from decimal import Decimal, InvalidOperation
import hashlib
import http.client
import json
from pathlib import Path
import platform
import statistics
import time
import uuid

from check_bid_atomicity import App, binary_from_roster, create_lot, pg_environment, post_bid, sql, state


def validate_bid(status, body, amount, request_id, seen_ids, allow_legacy_response):
    if status != 201 or not isinstance(body, dict) or body.get("success") is not True:
        raise AssertionError(f"bid {request_id}: expected accepted response, got {status}: {body}")
    bid_id = body.get("bid_id")
    if type(bid_id) is not int or bid_id <= 0 or bid_id in seen_ids:
        raise AssertionError(f"bid {request_id}: missing or repeated bid_id: {bid_id}")
    try:
        price = Decimal(str(body["new_price"]))
    except (InvalidOperation, KeyError, ValueError, TypeError) as exc:
        raise AssertionError(f"bid {request_id}: missing or invalid new_price: {body}") from exc
    if not price.is_finite() or price != Decimal(amount):
        raise AssertionError(f"bid {request_id}: expected price {amount}, got {price}")
    if body.get("request_id") != request_id and not (allow_legacy_response and "request_id" not in body):
        raise AssertionError(f"bid {request_id}: response has wrong request_id: {body}")
    seen_ids.add(bid_id)
    return bid_id


def validate_database(env, lot_id, expected_price, expected_count, read_state=state):
    price, count = read_state(env, lot_id)
    if Decimal(price) != Decimal(expected_price) or count != expected_count:
        raise AssertionError(f"lot {lot_id}: expected price/count {expected_price}/{expected_count}, got {price}/{count}")


def self_test():
    expected = {"success": True, "new_price": 120, "bid_id": 41, "request_id": "test-request"}
    seen = set()
    assert validate_bid(201, expected, 120, "test-request", seen, False) == 41
    failures = (
        (200, expected, 121, "test-request", set(), False),
        (201, {**expected, "success": False}, 120, "test-request", set(), False),
        (201, {**expected, "bid_id": 41}, 120, "test-request", {41}, False),
        (201, {**expected, "new_price": 119}, 120, "test-request", set(), False),
        (201, {**expected, "request_id": "wrong"}, 120, "test-request", set(), False),
        (201, {k: v for k, v in expected.items() if k != "request_id"}, 120, "test-request", set(), False),
    )
    for case in failures:
        try:
            validate_bid(*case)
        except AssertionError:
            continue
        raise AssertionError(f"invalid bid passed profile oracle: {case}")
    legacy = {k: v for k, v in expected.items() if k != "request_id"}
    assert validate_bid(201, legacy, 120, "test-request", set(), True) == 41
    try:
        validate_bid(201, {**expected, "request_id": "wrong"}, 120, "test-request", set(), True)
    except AssertionError:
        pass
    else:
        raise AssertionError("legacy mode accepted a mismatched request_id")
    validate_database(None, 1, 120, 2, lambda _env, _lot: ("120.00", 2))
    for observed in (("119.00", 2), ("120.00", 1)):
        try:
            validate_database(None, 1, 120, 2, lambda _env, _lot: observed)
        except AssertionError:
            continue
        raise AssertionError(f"invalid database state passed profile oracle: {observed}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir")
    parser.add_argument("--iterations", type=int, default=200)
    parser.add_argument("--pause", type=float, default=0,
                        help="seconds between warmup and samples, for attaching an allocation profiler")
    parser.add_argument("--out", type=Path, help="write verified raw samples and source hash as JSON")
    parser.add_argument("--allow-legacy-response", action="store_true",
                        help="control binary may omit request_id; all other result checks still apply")
    parser.add_argument("--self-test", action="store_true", help="run response-oracle controls without services")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("bid profile oracles: positive, legacy and nine negative controls passed")
        return
    if not args.build_dir:
        parser.error("--build-dir is required unless --self-test is used")
    if args.iterations < 1:
        parser.error("--iterations must be positive")
    if args.pause < 0:
        parser.error("--pause must be nonnegative")
    if args.out and args.out.exists():
        parser.error("--out already exists; choose a new path for this run")
    env = pg_environment()
    binary = binary_from_roster(args.build_dir)
    app = App(binary, env)
    lot_id = None
    try:
        app.wait_ready()
        lot_id = create_lot(env, "latency")
        conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
        seen_ids = set()
        try:
            for i in range(10):
                request_id = str(uuid.uuid4())
                status, body = post_bid(conn, lot_id, 2, 110 + i, request_id)
                validate_bid(status, body, 110 + i, request_id, seen_ids, args.allow_legacy_response)
            validate_database(env, lot_id, 119, 10)
            print(f"PID={app.proc.pid} ready for {args.iterations} accepted bids", flush=True)
            if args.pause:
                time.sleep(args.pause)
            samples = []
            for i in range(args.iterations):
                request_id = str(uuid.uuid4())
                start = time.perf_counter_ns()
                status, body = post_bid(conn, lot_id, 2, 120 + i, request_id)
                elapsed_ns = time.perf_counter_ns() - start
                bid_id = validate_bid(status, body, 120 + i, request_id, seen_ids, args.allow_legacy_response)
                samples.append({"latency_ns": elapsed_ns, "request_id": request_id,
                                "bid_id": bid_id, "new_price": body["new_price"]})
        finally:
            conn.close()
        validate_database(env, lot_id, 119 + args.iterations, 10 + args.iterations)
        ordered = sorted(sample["latency_ns"] / 1e6 for sample in samples)
        report = {"schema": "auction-bid-profile/1", "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                  "host": platform.node(), "iterations": args.iterations, "warmup": 10,
                  "allow_legacy_response": args.allow_legacy_response,
                  "database": {"price": str(119 + args.iterations), "bid_count": 10 + args.iterations},
                  "samples": samples}
        summary = {"p25_ms": ordered[int(0.25 * (len(ordered) - 1))],
                   "p50_ms": statistics.median(ordered),
                   "p75_ms": ordered[int(0.75 * (len(ordered) - 1))],
                   "p95_ms": ordered[int(0.95 * (len(ordered) - 1))],
                   "mean_ms": statistics.mean(ordered)}
        report["summary"] = summary
        app.stop()
        report["status"] = "complete"
        print(f"accepted={len(samples)} p25_ms={ordered[int(0.25 * (len(ordered) - 1))]:.3f} "
              f"p50_ms={summary['p50_ms']:.3f} "
              f"p75_ms={ordered[int(0.75 * (len(ordered) - 1))]:.3f} "
              f"p95_ms={ordered[int(0.95 * (len(ordered) - 1))]:.3f} "
              f"mean_ms={summary['mean_ms']:.3f}")
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    finally:
        app.close()
        if lot_id is not None:
            sql(f"DELETE FROM lots WHERE id={lot_id}", env)


if __name__ == "__main__":
    main()
