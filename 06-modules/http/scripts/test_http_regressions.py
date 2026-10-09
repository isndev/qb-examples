#!/usr/bin/env python3
"""Local HTTP regression checks for the REST and static-file examples.

Usage: test_http_regressions.py <rest-example-binary> <static-example-binary>
Each server runs in a private temporary directory and only binds localhost:8080.
"""

import contextlib
import http.client
import json
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time


PORT = 8080


def request(method, target, body=None, content_type=None):
    """Send the target exactly as written, including raw or encoded parent parts."""
    connection = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
    connection.putrequest(method, target)
    if content_type:
        connection.putheader("Content-Type", content_type)
    if body is not None:
        connection.putheader("Content-Length", str(len(body)))
    connection.endheaders(body)
    response = connection.getresponse()
    payload = response.read()
    status = response.status
    connection.close()
    return status, json.loads(payload)


def check(condition, message):
    if not condition:
        raise AssertionError(message)
    print("PASS", message)


def upload(content, filename="same.txt"):
    boundary = "qb-example-http-regression"
    body = (
        f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        "Content-Type: text/plain\r\n\r\n"
    ).encode() + content + f"\r\n--{boundary}--\r\n".encode()
    return request("POST", "/api/upload", body, f"multipart/form-data; boundary={boundary}")


@contextlib.contextmanager
def server(binary, workdir, ready_path):
    executable = workdir / binary.name
    shutil.copy2(binary, executable)
    # The Windows example build stages its runtime DLLs beside the executable.
    for runtime_dll in binary.parent.glob("*.dll"):
        shutil.copy2(runtime_dll, workdir / runtime_dll.name)
    log_path = workdir / "server.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen([str(executable)], cwd=workdir, stdout=log, stderr=log)
        try:
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError(f"server exited early: {log_path.read_text(errors='replace')}")
                try:
                    request("GET", ready_path)
                    break
                except (OSError, http.client.HTTPException):
                    time.sleep(0.05)
            else:
                raise RuntimeError(f"server did not bind: {log_path.read_text(errors='replace')}")
            yield
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def test_static(binary):
    with tempfile.TemporaryDirectory(prefix="qb-static-http-") as directory:
        root = pathlib.Path(directory)
        static = root / "resources" / "static"
        inside = static / "inside"
        outside = root / "resources" / "outside"
        inside.mkdir(parents=True)
        outside.mkdir()
        (inside / "inside-marker").write_text("inside")
        (outside / "outside-marker").write_text("outside")
        delete_marker = static / "delete-marker.txt"
        delete_marker.write_text("must remain")
        symlink_available = True
        try:
            (static / "outside-link").symlink_to(outside, target_is_directory=True)
            (static / "inside-link").symlink_to(inside, target_is_directory=True)
        except OSError:
            symlink_available = False  # Windows may require Developer Mode.

        with server(binary, root, "/browse"):
            status, data = request("GET", "/browse")
            check(status == 200 and any(item["name"] == "inside" for item in data["entries"]), "browse root is available")
            status, data = request("GET", "/browse/inside")
            check(status == 200 and any(item["name"] == "inside-marker" for item in data["entries"]), "browse child stays inside root")
            for path in ("/browse/../outside", "/browse/%2e%2e/outside", "/browse/%2e%2e%2foutside"):
                status, data = request("GET", path)
                check(status == 403 and "outside-marker" not in json.dumps(data), f"browse rejects {path}")
            if symlink_available:
                status, data = request("GET", "/browse/outside-link")
                check(status == 403 and "outside-marker" not in json.dumps(data), "browse rejects outward symlink")
                status, data = request("GET", "/browse/inside-link")
                check(status == 200 and any(item["name"] == "inside-marker" for item in data["entries"]), "browse accepts inward symlink")
            else:
                print("SKIP symlink policy: symlink creation unavailable")

            escaped_marker = "/api/files/%2e%2e%2fresources%2fstatic%2fdelete-marker.txt"
            for method in ("GET", "PUT", "DELETE"):
                body = b"{}" if method == "PUT" else None
                status, _ = request(method, escaped_marker + ("/metadata" if method == "PUT" else ""), body,
                                    "application/json" if method == "PUT" else None)
                check(status == 400 and delete_marker.read_text() == "must remain", f"{method} rejects decoded parent path")
            for unsafe_name in ("%2e", "%2e%2e", "%2e%2e%5cdelete-marker.txt", "C%3adelete-marker.txt", "%00"):
                status, _ = request("DELETE", "/api/files/" + unsafe_name)
                check(status == 400 and delete_marker.read_text() == "must remain", f"DELETE rejects unsafe name {unsafe_name}")

            first = b"first content"
            second = b"second content"
            status1, result1 = upload(first)
            status2, result2 = upload(second)
            check(status1 == status2 == 201, "two uploads succeed")
            name1, name2 = result1["filename"], result2["filename"]
            check(name1 != name2, "same-name uploads get distinct stored names")
            check((root / "uploads" / name1).read_bytes() == first, "first upload remains intact")
            check((root / "uploads" / name2).read_bytes() == second, "second upload contains its own bytes")
            status, _ = upload(b"unsafe", "../unsafe.txt")
            check(status == 400 and not (root / "unsafe.txt").exists(), "upload rejects a path-shaped filename")

            metadata_path = f"/api/files/{name1}"
            status, before = request("GET", metadata_path)
            check(status == 200, "uploaded file metadata is readable")
            valid = json.dumps({"description": "updated"}).encode()
            status, _ = request("PUT", metadata_path + "/metadata", valid, "application/json")
            check(status == 200, "valid metadata update succeeds")
            status, before = request("GET", metadata_path)
            check(status == 200 and before["description"] == "updated", "valid metadata update is stored")
            invalid = json.dumps({"description": "should not stick", "tags": 17}).encode()
            status, _ = request("PUT", metadata_path + "/metadata", invalid, "application/json")
            check(status == 400, "invalid metadata update is rejected")
            status, after = request("GET", metadata_path)
            check(status == 200 and after == before, "metadata remains unchanged after 400")
            status, _ = request("DELETE", f"/api/files/{name2}")
            check(status == 200 and not (root / "uploads" / name2).exists(), "valid DELETE removes its uploaded file")
            if symlink_available:
                file_link = root / "uploads" / "delete-link"
                try:
                    file_link.symlink_to(delete_marker)
                except OSError:
                    print("SKIP file symlink policy: symlink creation unavailable")
                else:
                    status, _ = request("DELETE", "/api/files/delete-link")
                    check(status == 200 and not file_link.is_symlink() and delete_marker.read_text() == "must remain",
                          "DELETE removes an upload symlink without touching its target")


def test_rest(binary):
    with tempfile.TemporaryDirectory(prefix="qb-rest-http-") as directory:
        root = pathlib.Path(directory)
        with server(binary, root, "/api/v1/books/1"):
            status, before = request("GET", "/api/v1/books/1")
            check(status == 200, "sample book is readable")
            valid = json.dumps({"title": "valid title"}).encode()
            status, _ = request("PATCH", "/api/v1/books/1", valid, "application/json")
            check(status == 200, "valid Book PATCH succeeds")
            status, before = request("GET", "/api/v1/books/1")
            check(status == 200 and before["title"] == "valid title", "valid Book PATCH is stored")
            invalid = json.dumps({"title": "should not stick", "year": "invalid"}).encode()
            status, _ = request("PATCH", "/api/v1/books/1", invalid, "application/json")
            check(status == 400, "invalid Book PATCH is rejected")
            status, after = request("GET", "/api/v1/books/1")
            check(status == 200 and after == before, "Book remains unchanged after 400")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_http_regressions.py <rest-example-binary> <static-example-binary>")
    rest, static = (pathlib.Path(arg).resolve() for arg in sys.argv[1:])
    with socket.socket() as probe:
        if probe.connect_ex(("127.0.0.1", PORT)) == 0:
            raise SystemExit("port 8080 is already in use")
    test_static(static)
    test_rest(rest)


if __name__ == "__main__":
    main()
