#!/usr/bin/env python3
"""Run PostgreSQL lessons against a disposable server and assert object ownership."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
from pathlib import Path


LESSONS = {
    "parameters": "Database initialized and statements prepared successfully.",
    "transactions": "=== transactions complete:",
    "types": "Data types example completed.",
    "errors": "Error scenarios complete.",
    "typed-rows": "=== typed rows complete:",
    "listen-notify": "=== listen/notify complete:",
    "callbacks-and-await": "=== callbacks and await complete:",
    "streaming-results": "=== streaming complete:",
}
PUBLIC_TABLES = (
    "users",
    "qb_tx_accounts",
    "data_types_test",
    "error_test_items",
    "qb_example_typed_rows",
    "qb_example_notify_jobs",
    "qb_example_callbacks",
    "qb_example_streaming",
)


def run(args, *, env=None, cwd=None, timeout=240):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          env=env, cwd=cwd, timeout=timeout, check=True).stdout


def postgres_tools():
    pg_config = shutil.which("pg_config")
    bins = Path(run([pg_config, "--bindir"]).strip()) if pg_config else None
    tools = {}
    for name in ("initdb", "pg_ctl", "psql"):
        path = shutil.which(name)
        if not path and bins:
            candidate = bins / (name + (".exe" if os.name == "nt" else ""))
            if candidate.exists():
                path = str(candidate)
        if not path:
            return None
        tools[name] = path
    return tools


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def check_lesson(executable, uri):
    env = dict(os.environ, QB_EXAMPLE_PG_URI=uri)
    try:
        output = run([str(executable)], env=env, cwd=executable.parent)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        raise AssertionError(f"{executable.name} failed: {exc.stdout}") from exc
    label = executable.name.removeprefix("qb-example-modules-pgsql-").removesuffix(".exe")
    required = LESSONS[label]
    # The errors lesson intentionally demonstrates failures. Its existing type-mismatch
    # diagnostic is outside this ownership check; its setup and cleanup still have to run.
    unexpected = "UNEXPECTED" in output if label != "errors" else "Error test schema and statements initialized." not in output
    if required not in output or unexpected or "Failed to " in output:
        raise AssertionError(f"{label}: missing {required!r} or unexpected failure:\n{output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, help="CMake build directory containing examples/")
    parser.add_argument("--strict", action="store_true", help="missing PostgreSQL tools fail rather than skip")
    args = parser.parse_args()
    tools = postgres_tools()
    if not tools:
        print("SKIP: initdb, pg_ctl and psql are required for disposable PostgreSQL check")
        return 1 if args.strict else 0

    suffix = ".exe" if os.name == "nt" else ""
    binary_dir = args.build_dir.resolve() / "examples" / "06-modules" / "pgsql"
    binaries = [binary_dir / f"qb-example-modules-pgsql-{name}{suffix}" for name in LESSONS]
    missing = [str(path) for path in binaries if not path.is_file()]
    if missing:
        raise RuntimeError("Missing built PostgreSQL examples: " + ", ".join(missing))

    with tempfile.TemporaryDirectory(prefix="qb-pg-ownership-") as temp:
        work = Path(temp)
        data = work / "data"
        port = free_port()
        run([tools["initdb"], "-D", str(data), "--auth=trust", "--no-instructions"])
        server_options = f"-p {port} -h 127.0.0.1"
        if os.name != "nt":
            server_options += f" -k {work}"
        run([tools["pg_ctl"], "-D", str(data), "-l", str(work / "postgres.log"), "-o", server_options, "start"])
        try:
            base = [tools["psql"], "-h", "127.0.0.1", "-p", str(port), "-v", "ON_ERROR_STOP=1", "-At"]

            def sql(statement, database="test", user="test"):
                command = base + ["-d", database]
                if user:
                    command += ["-U", user]
                return run(command + ["-c", statement]).strip()

            sql("CREATE ROLE test LOGIN PASSWORD 'test'", "postgres", None)
            sql("CREATE DATABASE test OWNER test", "postgres", None)
            for table in PUBLIC_TABLES:
                sql(f"CREATE TABLE public.{table} (marker TEXT NOT NULL)")
                sql(f"INSERT INTO public.{table} VALUES ('audit-sentinel')")
            sql("CREATE FUNCTION public.qb_example_notify_job() RETURNS trigger LANGUAGE plpgsql "
                "AS $$ BEGIN RETURN NEW; END $$")
            table_oids = {table: sql(f"SELECT 'public.{table}'::regclass::oid") for table in PUBLIC_TABLES}
            function_oid = sql("SELECT 'public.qb_example_notify_job()'::regprocedure::oid")
            before_function = sql("SELECT md5(pg_get_functiondef('public.qb_example_notify_job()'::regprocedure))")

            def assert_owned_objects_clean():
                for table in PUBLIC_TABLES:
                    marker = sql(f"SELECT marker FROM public.{table}")
                    oid = sql(f"SELECT 'public.{table}'::regclass::oid")
                    if marker != "audit-sentinel" or oid != table_oids[table]:
                        raise AssertionError(f"public.{table} changed or disappeared: marker={marker!r}, oid={oid}")
                if sql("SELECT 'public.qb_example_notify_job()'::regprocedure::oid") != function_oid:
                    raise AssertionError("public.qb_example_notify_job() identity changed")
                after_function = sql("SELECT md5(pg_get_functiondef('public.qb_example_notify_job()'::regprocedure))")
                if before_function != after_function:
                    raise AssertionError("public.qb_example_notify_job() changed or disappeared")
                leftovers = sql("SELECT count(*) FROM pg_class WHERE "
                                "(relname LIKE 'qb_tx_accounts_%' OR relname IN "
                                "('users', 'data_types_test', 'error_test_items', "
                                "'qb_example_typed_rows', 'qb_example_notify_jobs', "
                                "'qb_example_callbacks', 'qb_example_streaming')) "
                                "AND relnamespace <> 'public'::regnamespace")
                functions = sql("SELECT count(*) FROM pg_proc WHERE proname = 'qb_example_notify_job' "
                                "AND pronamespace <> 'public'::regnamespace")
                if leftovers != "0" or functions != "0":
                    raise AssertionError(f"{leftovers} run-owned tables and {functions} functions remain")

            uri = f"tcp://test:test@127.0.0.1:{port}[test]"
            for binary in binaries:
                check_lesson(binary, uri)
                assert_owned_objects_clean()
                print(f"PASS: {binary.name} isolated run")

            # Separate processes overlap on the same database. A fixed table/function/channel
            # would collide or replace another run's state.
            for binary in binaries:
                env = dict(os.environ, QB_EXAMPLE_PG_URI=uri)
                with ThreadPoolExecutor(max_workers=2) as pool:
                    futures = [pool.submit(run, [str(binary)], env=env, cwd=binary.parent) for _ in range(2)]
                    for future in futures:
                        output = future.result()
                        label = binary.name.removeprefix("qb-example-modules-pgsql-").removesuffix(".exe")
                        unexpected = "UNEXPECTED" in output if label != "errors" else "Error test schema and statements initialized." not in output
                        if LESSONS[label] not in output or unexpected or "Failed to " in output:
                            raise AssertionError(f"concurrent {binary.name}: {output}")
                assert_owned_objects_clean()
                print(f"PASS: {binary.name} two concurrent runs")

            callbacks = binary_dir / f"qb-example-modules-pgsql-callbacks-and-await{suffix}"
            sql("CREATE ROLE qb_example_no_temp LOGIN", "postgres", None)
            sql("CREATE DATABASE qb_example_denied OWNER test", "postgres", None)
            sql("REVOKE TEMPORARY ON DATABASE qb_example_denied FROM PUBLIC", "postgres", None)
            sql("GRANT CONNECT ON DATABASE qb_example_denied TO qb_example_no_temp", "postgres", None)
            sql("CREATE TABLE public.qb_example_callbacks "
                "(id SERIAL PRIMARY KEY, name TEXT NOT NULL, score DOUBLE PRECISION NOT NULL)", "qb_example_denied")
            sql("INSERT INTO public.qb_example_callbacks(name, score) VALUES ('audit-sentinel', 1)", "qb_example_denied")
            sql("GRANT USAGE ON SCHEMA public TO qb_example_no_temp", "qb_example_denied")
            sql("GRANT SELECT, INSERT ON public.qb_example_callbacks TO qb_example_no_temp", "qb_example_denied")
            public_oid = sql("SELECT 'public.qb_example_callbacks'::regclass::oid", "qb_example_denied")
            denied_uri = f"tcp://qb_example_no_temp@127.0.0.1:{port}[qb_example_denied]"
            denied = subprocess.run([str(callbacks)], cwd=callbacks.parent,
                                    env=dict(os.environ, QB_EXAMPLE_PG_URI=denied_uri),
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=240)
            if denied.returncode == 0 or "[setup] temporary table setup failed" not in denied.stdout \
                    or "[file]" in denied.stdout:
                raise AssertionError(f"callback setup failure continued into later statements:\n{denied.stdout}")
            rows = sql("SELECT count(*), min(name) FROM public.qb_example_callbacks", "qb_example_denied")
            after_oid = sql("SELECT 'public.qb_example_callbacks'::regclass::oid", "qb_example_denied")
            if rows != "1|audit-sentinel" or after_oid != public_oid:
                raise AssertionError(f"callback setup failure touched public table: {rows}, OID {after_oid}")
            print("PASS: refused CREATE TEMP stops callbacks before public-table access")

            transactions = binary_dir / f"qb-example-modules-pgsql-transactions{suffix}"
            sql("CREATE DATABASE qb_example_drop_denied OWNER test", "postgres", None)
            sql("CREATE FUNCTION public.qb_example_reject_drop() RETURNS event_trigger "
                "LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'DROP blocked by ownership test'; END $$",
                "qb_example_drop_denied", None)
            sql("CREATE EVENT TRIGGER qb_example_block_drop ON ddl_command_start "
                "WHEN TAG IN ('DROP TABLE') EXECUTE FUNCTION public.qb_example_reject_drop()",
                "qb_example_drop_denied", None)
            blocked_uri = f"tcp://test:test@127.0.0.1:{port}[qb_example_drop_denied]"
            blocked = subprocess.run([str(transactions)], cwd=transactions.parent,
                                     env=dict(os.environ, QB_EXAMPLE_PG_URI=blocked_uri),
                                     text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     timeout=240)
            match = re.search(r"Failed to drop owned table (qb_tx_accounts_[0-9a-f]{32}):", blocked.stdout)
            if blocked.returncode == 0 or not match or "=== transactions complete:" in blocked.stdout:
                raise AssertionError(f"refused DROP was not reported as failure:\n{blocked.stdout}")
            owned_table = match.group(1)
            if sql(f"SELECT to_regclass('public.{owned_table}') IS NOT NULL", "qb_example_drop_denied") != "t":
                raise AssertionError("refused DROP did not leave the expected run-owned table")
            sql("DROP EVENT TRIGGER qb_example_block_drop", "qb_example_drop_denied", None)
            sql("DROP FUNCTION public.qb_example_reject_drop()", "qb_example_drop_denied", None)
            sql(f"DROP TABLE public.{owned_table}", "qb_example_drop_denied")
            if sql(f"SELECT to_regclass('public.{owned_table}') IS NULL", "qb_example_drop_denied") != "t":
                raise AssertionError("repair cleanup left the run-owned table behind")
            print("PASS: refused DROP exits nonzero, identifies table, and permits repair cleanup")

            print(f"PASS: {len(PUBLIC_TABLES)} public sentinels and function intact; no run-owned tables remain")
        finally:
            run([tools["pg_ctl"], "-D", str(data), "-m", "fast", "stop"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
