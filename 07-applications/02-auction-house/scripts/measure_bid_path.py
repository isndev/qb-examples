#!/usr/bin/env python3
"""Measure accepted bid HTTP latency on one persistent session and live PostgreSQL.

Run the same script against before/after binaries on an otherwise idle host.
The UUID is sent in both runs; the older handler ignores the extra JSON field.
"""

import argparse
import http.client
import statistics
import time

from check_bid_atomicity import App, binary_from_roster, create_lot, pg_environment, post_bid, sql


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--iterations", type=int, default=200)
    parser.add_argument("--pause", type=float, default=0,
                        help="seconds between warmup and samples, for attaching an allocation profiler")
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error("--iterations must be positive")
    env = pg_environment()
    app = App(binary_from_roster(args.build_dir), env)
    lot_id = None
    try:
        app.wait_ready()
        lot_id = create_lot(env, "latency")
        conn = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
        try:
            for i in range(10):
                status, _ = post_bid(conn, lot_id, 2, 110 + i)
                if status != 201:
                    raise AssertionError(f"warmup bid {i}: HTTP {status}")
            print(f"PID={app.proc.pid} ready for {args.iterations} accepted bids", flush=True)
            if args.pause:
                time.sleep(args.pause)
            samples = []
            for i in range(args.iterations):
                start = time.perf_counter_ns()
                status, _ = post_bid(conn, lot_id, 2, 120 + i)
                samples.append((time.perf_counter_ns() - start) / 1e6)
                if status != 201:
                    raise AssertionError(f"sample bid {i}: HTTP {status}")
        finally:
            conn.close()
        ordered = sorted(samples)
        print(f"accepted={len(samples)} p25_ms={ordered[int(0.25 * (len(ordered) - 1))]:.3f} "
              f"p50_ms={statistics.median(samples):.3f} "
              f"p75_ms={ordered[int(0.75 * (len(ordered) - 1))]:.3f} "
              f"p95_ms={ordered[int(0.95 * (len(ordered) - 1))]:.3f} "
              f"mean_ms={statistics.mean(samples):.3f}")
        app.stop()
    finally:
        app.close()
        if lot_id is not None:
            sql(f"DELETE FROM lots WHERE id={lot_id}", env)


if __name__ == "__main__":
    main()
